#include "RollbackSnapshot.hpp"
#include "RollbackProductionSummary.hpp"
#include "RollbackTerminalCheckpointAuthority.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
    struct TerminalCheckpointState
    {
        uint64_t canonical_hash {0};
        uint64_t combined_hash {0};
        uint32_t value {0};
        bool integrity_valid {false};

        void clear() noexcept
        {
            canonical_hash = 0;
            combined_hash = 0;
            value = 0;
            integrity_valid = false;
        }
    };

    Horse::RollbackSnapshotFrame make_snapshot()
    {
        Horse::RollbackSnapshotFrame snapshot {};
        snapshot.schema_hash = 0xCC06;
        snapshot.bytes.resize(0xCC);
        for (size_t i = 0; i < snapshot.bytes.size(); ++i)
            snapshot.bytes[i] = static_cast<uint8_t>(i * 17u + 3u);
        snapshot.ranges.push_back({
            0, 0x1000, 0, 0xC0, 0,
            Horse::RollbackCanonicalPolicy::LuxMoveSchedStateArray});
        snapshot.ranges.push_back({
            1, 0x14470D0C4ull, 0xC0, 4, 0,
            Horse::RollbackCanonicalPolicy::LuxBattleNativeFrameCounter});
        snapshot.ranges.push_back({
            2, 0x14470DED8ull, 0xC4, 4, 0,
            Horse::RollbackCanonicalPolicy::LuxBattleCollisionCooldown});
        snapshot.ranges.push_back({
            3, 0x1440F3CACull, 0xC8, 4, 0,
            Horse::RollbackCanonicalPolicy::LuxBattleCollisionOwner});
        const uint32_t current = 3387;
        const uint32_t last = 3385;
        const uint32_t owner = 1;
        std::memcpy(snapshot.bytes.data() + 0xC0,
            &current, sizeof(current));
        std::memcpy(snapshot.bytes.data() + 0xC4,
            &last, sizeof(last));
        std::memcpy(snapshot.bytes.data() + 0xC8,
            &owner, sizeof(owner));
        return snapshot;
    }

    Horse::RollbackProductionFrameSummary make_summary()
    {
        Horse::RollbackProductionFrameSummary summary {};
        summary.epoch = 0x101;
        summary.frame = 77;
        summary.canonical_hash = 0x102;
        for (size_t i = 0; i < 4; ++i)
            summary.component_hash[i] = 0x110 + i;
        summary.hgcpu_peer.chara_stream_hash[0] = 0x201;
        summary.hgcpu_peer.chara_chunk_hash[1][0] = 0x202;
        summary.hgcpu_peer.motion_provider_hash[0][0] = 0x203;
        summary.hgcpu_peer.motion_provider_age[0][0] = 2;
        summary.native_round_state_hash = 0x301;
        summary.native_simulation_state_hash = 0x302;
        summary.palette_variant_canonical_hash = 0x303;
        summary.palette_variant_active_mask = 0x02;
        summary.terminal_round_result = 1;
        summary.round_result_type = 4;
        summary.input[0] = 0x40;
        summary.input[1] = 0x400;
        return summary;
    }

    bool rejects_mutation(
        const Horse::RollbackProductionFrameSummary& baseline,
        void (*mutate)(Horse::RollbackProductionFrameSummary&))
    {
        auto changed = baseline;
        mutate(changed);
        return !Horse::RollbackProductionSummaryStateMatches(
            baseline, changed);
    }
}

int main()
{
    auto snapshot = make_snapshot();
    auto summary = make_summary();
    const auto detail = Horse::BuildRollbackProductionSummaryDetails(
        snapshot, summary);
    if (!detail.ok || detail.range_count != 4
        || detail.ccpu_range_count != 1
        || summary.ccpu_canonical_hash == 0
        || !Horse::RollbackProductionSummaryStateMatches(summary, summary))
    {
        std::cerr << "valid production summary rejected: "
                  << detail.failure << '\n';
        return 1;
    }
    auto terminal_candidate = summary;
    terminal_candidate.flags =
        Horse::kRollbackProductionSummaryTerminalCandidate;
    Horse::RollbackProductionTerminalSummaryArchive terminal_archive {};
    terminal_archive.retain(summary);
    const auto* retained_summary = terminal_archive.find(
        summary.epoch, summary.frame);
    const bool retained_initial = retained_summary
        && retained_summary->canonical_hash == summary.canonical_hash;
    auto archive_wrapped_summary = summary;
    archive_wrapped_summary.frame += static_cast<uint32_t>(
        Horse::kRollbackProductionSummaryWindowCapacity);
    terminal_archive.retain(archive_wrapped_summary);
    const bool terminal_archive_ok = retained_initial
        && terminal_archive.find(summary.epoch, summary.frame) == nullptr
        && terminal_archive.find(
            archive_wrapped_summary.epoch,
            archive_wrapped_summary.frame) != nullptr;
    terminal_archive.clear();
    if (!terminal_archive_ok
        || terminal_archive.find(
            archive_wrapped_summary.epoch,
            archive_wrapped_summary.frame) != nullptr)
    {
        std::cerr << "terminal summary archive retention failed\n";
        return 1;
    }
    auto peer_terminal_candidate = terminal_candidate;
    if (!Horse::RollbackProductionTerminalCandidateValid(
            terminal_candidate)
        || !Horse::RollbackProductionTerminalCandidatesMatch(
            terminal_candidate, peer_terminal_candidate)
        || Horse::ClassifyRollbackProductionSummaryChannel(
                terminal_candidate.flags)
            != Horse::RollbackProductionSummaryChannel::TerminalCandidate
        || Horse::ClassifyRollbackProductionSummaryChannel(
                Horse::kRollbackProductionSummaryConfirmed)
            != Horse::RollbackProductionSummaryChannel::Consensus
        || Horse::ClassifyRollbackProductionSummaryChannel(0)
            != Horse::RollbackProductionSummaryChannel::Invalid
        || !Horse::RollbackProductionTerminalPairProofMatches(
            true, terminal_candidate.frame,
            terminal_candidate.canonical_hash,
            terminal_candidate, peer_terminal_candidate))
    {
        std::cerr << "matching terminal candidates rejected\n";
        return 1;
    }
    auto pre_edge_local = summary;
    pre_edge_local.terminal_round_result = 0;
    pre_edge_local.round_result_type = 0;
    auto terminal_tail = pre_edge_local;
    terminal_tail.flags = Horse::kRollbackProductionSummaryTerminalTail;
    if (!Horse::RollbackProductionTerminalTailValid(terminal_tail)
        || !Horse::RollbackProductionTerminalEvidenceValid(terminal_tail)
        || Horse::RollbackProductionTerminalCandidateValid(terminal_tail)
        || Horse::ClassifyRollbackProductionSummaryChannel(
                terminal_tail.flags)
            != Horse::RollbackProductionSummaryChannel::TerminalCandidate
        || !Horse::RollbackProductionTerminalEvidenceMatches(
            pre_edge_local, terminal_tail)
        || !Horse::RollbackProductionTerminalPairProofMatches(
            true, terminal_tail.frame, terminal_tail.canonical_hash,
            pre_edge_local, terminal_tail))
    {
        std::cerr << "valid pre-edge terminal tail evidence rejected\n";
        return 1;
    }
    auto malformed_tail = terminal_tail;
    malformed_tail.input[0] ^= 1;
    if (Horse::RollbackProductionTerminalEvidenceMatches(
            pre_edge_local, malformed_tail)
        || Horse::RollbackProductionTerminalPairProofMatches(
            true, malformed_tail.frame, malformed_tail.canonical_hash,
            pre_edge_local, malformed_tail))
    {
        std::cerr << "mutated pre-edge terminal tail evidence accepted\n";
        return 1;
    }
    auto terminal_edge_tail = terminal_candidate;
    terminal_edge_tail.flags = Horse::kRollbackProductionSummaryTerminalTail;
    if (!Horse::RollbackProductionTerminalTailValid(terminal_edge_tail)
        || !Horse::RollbackProductionTerminalEvidenceValid(
            terminal_edge_tail)
        || !Horse::RollbackProductionTerminalEvidenceMatches(
            terminal_candidate, terminal_edge_tail))
    {
        std::cerr << "pre-terminal edge-state tail rejected\n";
        return 1;
    }
    auto mismatched_edge_tail = terminal_edge_tail;
    ++mismatched_edge_tail.round_result_type;
    if (Horse::RollbackProductionTerminalEvidenceMatches(
            terminal_candidate, mismatched_edge_tail))
    {
        std::cerr << "mismatched edge-state tail accepted\n";
        return 1;
    }
    using TailAction =
        Horse::RollbackProductionTerminalTailEvidenceAction;
    if (Horse::ClassifyRollbackProductionTerminalTailEvidence(
            false, true, 1882, 1984, 1882)
            != TailAction::StageUntilAgreement
        || Horse::ClassifyRollbackProductionTerminalTailEvidence(
            true, true, 1883, 1984, 1882)
            != TailAction::DiscardRetired
        || Horse::ClassifyRollbackProductionTerminalTailEvidence(
            true, true, 1882, 1984, 1882) != TailAction::Match
        || Horse::ClassifyRollbackProductionTerminalTailEvidence(
            true, true, 1882, 1984, 1984) != TailAction::Reject
        || Horse::ClassifyRollbackProductionTerminalTailEvidence(
            true, false, 0, 1984, 1882) != TailAction::Reject)
    {
        std::cerr << "terminal tail phase-skew classification failed\n";
        return 1;
    }
    if (!Horse::RollbackProductionQuiescedTerminalEvidenceRetryRequired(
            true, true, false)
        || Horse::RollbackProductionQuiescedTerminalEvidenceRetryRequired(
            true, true, true)
        || Horse::RollbackProductionQuiescedTerminalEvidenceRetryRequired(
            true, false, false)
        || Horse::RollbackProductionQuiescedTerminalEvidenceRetryRequired(
            false, true, false))
    {
        std::cerr << "quiesced terminal evidence retry contract failed\n";
        return 1;
    }
    if (!Horse::RollbackProductionOrdinarySummaryCommitAllowed(
            false, false, 1989, 1983)
        || Horse::RollbackProductionOrdinarySummaryCommitAllowed(
            true, false, 1983, 1983)
        || Horse::RollbackProductionOrdinarySummaryCommitAllowed(
            true, false, 1984, 1983)
        || Horse::RollbackProductionOrdinarySummaryCommitAllowed(
            true, true, 1980, 1983))
    {
        std::cerr << "terminal ordinary-summary freeze failed\n";
        return 1;
    }
    if (Horse::RollbackProductionTerminalTailMissingMask(
            true, true, true) != 0
        || Horse::RollbackProductionTerminalTailMissingMask(
            false, true, true) != 1
        || Horse::RollbackProductionTerminalTailMissingMask(
            true, false, true) != 2
        || Horse::RollbackProductionTerminalTailMissingMask(
            true, true, false) != 4
        || Horse::RollbackProductionTerminalTailMissingMask(
            false, false, false) != 7)
    {
        std::cerr << "terminal tail missing-slot diagnostic failed\n";
        return 1;
    }
    peer_terminal_candidate.input[1] ^= 1;
    if (Horse::RollbackProductionTerminalCandidatesMatch(
            terminal_candidate, peer_terminal_candidate)
        || Horse::RollbackProductionTerminalPairProofMatches(
            true, terminal_candidate.frame,
            terminal_candidate.canonical_hash,
            terminal_candidate, peer_terminal_candidate))
    {
        std::cerr << "terminal candidate input mismatch accepted\n";
        return 1;
    }
    peer_terminal_candidate = terminal_candidate;
    peer_terminal_candidate.round_result_type ^= 1;
    if (Horse::RollbackProductionTerminalCandidatesMatch(
            terminal_candidate, peer_terminal_candidate))
    {
        std::cerr << "terminal candidate result mismatch accepted\n";
        return 1;
    }
    peer_terminal_candidate = terminal_candidate;
    peer_terminal_candidate.component_hash[0] ^= 1;
    if (Horse::RollbackProductionTerminalCandidatesMatch(
            terminal_candidate, peer_terminal_candidate))
    {
        std::cerr << "terminal candidate state mismatch accepted\n";
        return 1;
    }
    peer_terminal_candidate = terminal_candidate;
    peer_terminal_candidate.terminal_round_result = 0;
    if (Horse::RollbackProductionTerminalCandidatesMatch(
            terminal_candidate, peer_terminal_candidate)
        || Horse::RollbackProductionTerminalCandidateValid(
            peer_terminal_candidate))
    {
        std::cerr << "one-sided terminal candidate accepted\n";
        return 1;
    }
    peer_terminal_candidate = terminal_candidate;
    peer_terminal_candidate.flags =
        Horse::kRollbackProductionSummaryConfirmed;
    if (Horse::RollbackProductionTerminalCandidateValid(
            peer_terminal_candidate))
    {
        std::cerr << "ordinary summary accepted as terminal candidate\n";
        return 1;
    }
    peer_terminal_candidate = terminal_candidate;
    if (Horse::RollbackProductionTerminalPairProofMatches(
            true, terminal_candidate.frame + 128u,
            terminal_candidate.canonical_hash,
            terminal_candidate, peer_terminal_candidate)
        || Horse::RollbackProductionTerminalPairProofMatches(
            true, terminal_candidate.frame,
            terminal_candidate.canonical_hash ^ 1u,
            terminal_candidate, peer_terminal_candidate))
    {
        std::cerr << "aliased terminal pair proof accepted\n";
        return 1;
    }
    using ReplacementAction =
        Horse::RollbackTerminalCandidateReplacementAction;
    if (Horse::DecideRollbackTerminalCandidateReplacement(
            false, false, false, false) != ReplacementAction::Keep
        || Horse::DecideRollbackTerminalCandidateReplacement(
            true, true, true, true) != ReplacementAction::Keep
        || Horse::DecideRollbackTerminalCandidateReplacement(
            true, false, false, false) != ReplacementAction::Revoke
        || Horse::DecideRollbackTerminalCandidateReplacement(
            true, false, true, false) != ReplacementAction::Revoke
        || Horse::DecideRollbackTerminalCandidateReplacement(
            false, false, true, false) != ReplacementAction::Revoke
        || Horse::DecideRollbackTerminalCandidateReplacement(
            true, false, true, true)
            != ReplacementAction::RejectImmutable)
    {
        std::cerr << "terminal candidate replacement decision failed\n";
        return 1;
    }
    using ReceiptAction = Horse::RollbackTerminalCandidateReceiptAction;
    Horse::RollbackProductionSummaryAckWindow completed_ack {};
    completed_ack.epoch = 0x200;
    if (!Horse::RollbackProductionSummaryAckWindowStructurallyValid(
            completed_ack)
        || !Horse::RollbackProductionCompletedEpochTrafficDiscardable(
            true, 0x200, completed_ack.epoch, true)
        || Horse::RollbackProductionCompletedEpochTrafficDiscardable(
            false, 0x200, completed_ack.epoch, true)
        || Horse::RollbackProductionCompletedEpochTrafficDiscardable(
            true, 0x200, 0x300, true)
        || Horse::RollbackProductionCompletedEpochTrafficDiscardable(
            true, 0x200, completed_ack.epoch, false)
        || !Horse::RollbackTerminalCandidateSupersededByAcceptedTerminal(
            true, 0x200, 1989, 0x200, 1986)
        || !Horse::RollbackTerminalCandidateSupersededByAcceptedTerminal(
            true, 0x200, 1989, 0x200, 1990)
        || Horse::RollbackTerminalCandidateSupersededByAcceptedTerminal(
            true, 0x200, 1989, 0x200, 1989)
        || Horse::RollbackTerminalCandidateSupersededByAcceptedTerminal(
            true, 0x200, 1989, 0x300, 1986)
        || Horse::RollbackTerminalCandidateSupersededByAcceptedTerminal(
            false, 0x200, 1989, 0x200, 1986)
        || Horse::ClassifyRollbackTerminalCandidateReceipt(
            0x200, 0x100, 0x200, true, false, false, false)
            != ReceiptAction::Accept
        || Horse::ClassifyRollbackTerminalCandidateReceipt(
            0x200, 0x100, 0x200, false, true, true, false)
            != ReceiptAction::DiscardExactDuplicate
        || Horse::ClassifyRollbackTerminalCandidateReceipt(
            0x200, 0x100, 0x100, true, false, false, false)
            != ReceiptAction::DiscardCompletedRound
        || Horse::ClassifyRollbackTerminalCandidateReceipt(
            0x200, 0x100, 0x200, false, true, false, true)
            != ReceiptAction::DiscardSupersededCandidate
        || Horse::ClassifyRollbackTerminalCandidateReceipt(
            0x200, 0x100, 0x200, false, true, false, false)
            != ReceiptAction::Reject
        || Horse::ClassifyRollbackTerminalCandidateReceipt(
            0x200, 0x100, 0x300, true, false, false, true)
            != ReceiptAction::Reject
        || Horse::ClassifyRollbackTerminalCandidateReceipt(
            0x200, 0x100, 0x200, false, false, false, true)
            != ReceiptAction::Reject)
    {
        std::cerr << "terminal candidate receipt classification failed\n";
        return 1;
    }
    using FrontierAction =
        Horse::RollbackProductionTerminalFrontierAction;
    if (Horse::RollbackProductionTerminalTailResendBudget(1) != 1
        || Horse::RollbackProductionTerminalTailResendBudget(7) != 7
        || Horse::RollbackProductionTerminalTailResendBudget(8) != 8
        || Horse::RollbackProductionTerminalTailResendBudget(37) != 8
        || Horse::RollbackProductionTerminalTailResendBudget(128) != 8)
    {
        std::cerr << "terminal tail resend budget failed\n";
        return 1;
    }
    if (Horse::DecideRollbackProductionTerminalFrontierAction(
            false, false, true, 1984, true, 1980,
            false, false, 1983) != FrontierAction::None
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, true, true, 1984, true, 1980,
            false, false, 1983) != FrontierAction::None
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1977, true, 1980,
            true, false, 1983)
            != FrontierAction::FinalizePairProofTail
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, false, 0, true, 1980,
            true, false, 1983)
            != FrontierAction::PollAndRetry
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1980, false, 0,
            true, false, 1983)
            != FrontierAction::FinalizePairProofTail
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1981, true, 1980,
            false, false, 1983)
            != FrontierAction::PollAndRetry
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1981, true, 1980,
            true, false, 1983)
            != FrontierAction::FinalizePairProofTail
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1981, true, 1980,
            true, true, 1983)
            != FrontierAction::Quiesce
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1984, true, 1980,
            false, false, 1983)
            != FrontierAction::Quiesce
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1984, true, 1989,
            false, false, 1983)
            != FrontierAction::Quiesce
        || Horse::DecideRollbackProductionTerminalFrontierAction(
            true, false, true, 1983, true, 1989,
            true, false, 1983)
            != FrontierAction::FinalizePairProofTail)
    {
        std::cerr << "terminal frontier recovery decision failed\n";
        return 1;
    }
    if (!Horse::RollbackTerminalCandidateProposalEvidenceReady(
            true, 77, 0x102, 77, 0x102,
            true, true, 77, true)
        || Horse::RollbackTerminalCandidateProposalEvidenceReady(
            true, 77, 0x102, 77, 0x102,
            true, true, 77, false)
        || Horse::RollbackTerminalCandidateProposalEvidenceReady(
            true, 77, 0x102, 77, 0x102,
            false, true, 77, true)
        || Horse::RollbackTerminalCandidateProposalEvidenceReady(
            true, 78, 0x102, 77, 0x102,
            true, true, 77, true))
    {
        std::cerr << "unretained terminal proposal evidence accepted\n";
        return 1;
    }
    const auto validate_terminal_state =
        [](const TerminalCheckpointState& state) noexcept {
            return state.integrity_valid;
        };
    constexpr uint64_t terminal_epoch = 0x501;
    constexpr uint32_t terminal_frame = 77;
    constexpr uint64_t terminal_integrity = 0x701;
    constexpr uint64_t terminal_canonical = 0x801;
    Horse::RollbackSnapshotStore<TerminalCheckpointState, 4>
        terminal_store {};
    Horse::RollbackSnapshotStore<TerminalCheckpointState, 4>
        rolling_store {};
    TerminalCheckpointState terminal_state {
        terminal_canonical, terminal_integrity, 1, true};
    TerminalCheckpointState rolling_state {
        terminal_canonical, terminal_integrity, 2, true};
    Horse::RollbackSnapshotHandle saved_terminal {};
    Horse::RollbackSnapshotHandle saved_rolling {};
    const auto terminal_saved = terminal_store.save(
        terminal_epoch, terminal_frame, terminal_integrity,
        terminal_canonical, terminal_state,
        Horse::RollbackFrameStamp::From(terminal_frame), 1,
        saved_terminal);
    const auto rolling_saved = rolling_store.save(
        terminal_epoch, terminal_frame, terminal_integrity,
        terminal_canonical, rolling_state,
        Horse::RollbackFrameStamp::From(terminal_frame), 1,
        saved_rolling);
    Horse::RollbackSnapshotHandle selected_handle {};
    const TerminalCheckpointState* selected_state = nullptr;
    const auto selected_terminal =
        Horse::SelectRollbackTerminalCheckpointAuthority<
            TerminalCheckpointState>(
            terminal_store, rolling_store, terminal_epoch, terminal_frame,
            terminal_canonical, validate_terminal_state,
            selected_handle, selected_state);
    if (!terminal_saved.ok || !rolling_saved.ok
        || selected_terminal
            != Horse::RollbackTerminalCheckpointSource::TerminalStore
        || !selected_state || selected_state->value != 1
        || selected_handle.generation != saved_terminal.generation)
    {
        std::cerr << "terminal store authority was not preferred\n";
        return 1;
    }

    Horse::RollbackSnapshotStore<TerminalCheckpointState, 4>
        wrong_terminal_store {};
    TerminalCheckpointState wrong_terminal_state {
        terminal_canonical + 1u, terminal_integrity, 3, true};
    Horse::RollbackSnapshotHandle wrong_terminal_handle {};
    const auto wrong_terminal_saved = wrong_terminal_store.save(
        terminal_epoch, terminal_frame, terminal_integrity,
        terminal_canonical + 1u, wrong_terminal_state,
        Horse::RollbackFrameStamp::From(terminal_frame), 1,
        wrong_terminal_handle);
    selected_handle = {};
    selected_state = nullptr;
    const auto selected_rolling =
        Horse::SelectRollbackTerminalCheckpointAuthority<
            TerminalCheckpointState>(
            wrong_terminal_store, rolling_store, terminal_epoch,
            terminal_frame, terminal_canonical, validate_terminal_state,
            selected_handle, selected_state);
    if (!wrong_terminal_saved.ok
        || selected_rolling
            != Horse::RollbackTerminalCheckpointSource::RollingStore
        || !selected_state || selected_state->value != 2
        || selected_handle.generation != saved_rolling.generation)
    {
        std::cerr << "wrong terminal hash did not fall back to rolling store\n";
        return 1;
    }

    Horse::RollbackSnapshotStore<TerminalCheckpointState, 4>
        invalid_terminal_store {};
    Horse::RollbackSnapshotStore<TerminalCheckpointState, 4>
        valid_fallback_store {};
    TerminalCheckpointState invalid_terminal_state {
        terminal_canonical, terminal_integrity, 4, false};
    TerminalCheckpointState valid_fallback_state {
        terminal_canonical, terminal_integrity, 5, true};
    Horse::RollbackSnapshotHandle invalid_terminal_handle {};
    Horse::RollbackSnapshotHandle valid_fallback_handle {};
    const auto invalid_terminal_saved = invalid_terminal_store.save(
        terminal_epoch, terminal_frame, terminal_integrity,
        terminal_canonical, invalid_terminal_state,
        Horse::RollbackFrameStamp::From(terminal_frame), 1,
        invalid_terminal_handle);
    const auto valid_fallback_saved = valid_fallback_store.save(
        terminal_epoch, terminal_frame, terminal_integrity,
        terminal_canonical, valid_fallback_state,
        Horse::RollbackFrameStamp::From(terminal_frame), 1,
        valid_fallback_handle);
    selected_handle = {};
    selected_state = nullptr;
    const auto selected_after_integrity_failure =
        Horse::SelectRollbackTerminalCheckpointAuthority<
            TerminalCheckpointState>(
            invalid_terminal_store, valid_fallback_store,
            terminal_epoch, terminal_frame, terminal_canonical,
            validate_terminal_state, selected_handle, selected_state);
    if (!invalid_terminal_saved.ok || !valid_fallback_saved.ok
        || selected_after_integrity_failure
            != Horse::RollbackTerminalCheckpointSource::RollingStore
        || !selected_state || selected_state->value != 5
        || selected_handle.generation != valid_fallback_handle.generation)
    {
        std::cerr << "invalid terminal state did not fall back to rolling store\n";
        return 1;
    }

    Horse::RollbackSnapshotStore<TerminalCheckpointState, 4>
        invalid_rolling_store {};
    TerminalCheckpointState invalid_rolling_state {
        terminal_canonical, terminal_integrity, 6, false};
    Horse::RollbackSnapshotHandle invalid_rolling_handle {};
    const auto invalid_rolling_saved = invalid_rolling_store.save(
        terminal_epoch, terminal_frame, terminal_integrity,
        terminal_canonical, invalid_rolling_state,
        Horse::RollbackFrameStamp::From(terminal_frame), 1,
        invalid_rolling_handle);
    selected_handle = saved_terminal;
    selected_state = &terminal_state;
    const auto unavailable =
        Horse::SelectRollbackTerminalCheckpointAuthority<
            TerminalCheckpointState>(
            invalid_terminal_store, invalid_rolling_store,
            terminal_epoch, terminal_frame, terminal_canonical,
            validate_terminal_state, selected_handle, selected_state);
    if (!invalid_rolling_saved.ok
        || unavailable != Horse::RollbackTerminalCheckpointSource::None
        || selected_handle.valid() || selected_state)
    {
        std::cerr << "unavailable checkpoint published authority\n";
        return 1;
    }

    // Both stores deliberately produce identical handle metadata. A terminal
    // restore must remain bound to the canonical terminal store selected by
    // the caller rather than probing another store with the same handle.
    Horse::RollbackSnapshotHandle colliding_handle {};
    const TerminalCheckpointState* colliding_state = nullptr;
    if (!Horse::FindRollbackTerminalCheckpointAuthority<
            TerminalCheckpointState>(
            terminal_store, terminal_epoch, terminal_frame,
            terminal_canonical, validate_terminal_state,
            colliding_handle, colliding_state)
        || !colliding_state || colliding_state->value != 1
        || colliding_handle.epoch != saved_rolling.epoch
        || colliding_handle.frame != saved_rolling.frame
        || colliding_handle.generation != saved_rolling.generation
        || colliding_handle.integrity_hash != saved_rolling.integrity_hash
        || colliding_handle.canonical_hash != saved_rolling.canonical_hash)
    {
        std::cerr << "colliding checkpoint switched stores\n";
        return 1;
    }

    std::array<Horse::RollbackProductionFrameSummary, 4> speculative {};
    std::array<bool, 4> speculative_valid {};
    speculative[0] = summary;
    speculative[0].frame = 76;
    speculative[1] = summary;
    speculative[1].frame = 77;
    speculative[2] = summary;
    speculative[2].frame = 78;
    speculative[3] = summary;
    speculative[3].frame = 79;
    speculative_valid.fill(true);
    if (!Horse::CanInvalidateRollbackProductionSummariesAfter(
            speculative, speculative_valid, 77)
        || !Horse::InvalidateRollbackProductionSummariesAfter(
            speculative, speculative_valid, 77)
        || !speculative_valid[0] || !speculative_valid[1]
        || speculative_valid[2] || speculative_valid[3])
    {
        std::cerr << "speculative summary Load invalidation failed\n";
        return 1;
    }
    speculative[2] = summary;
    speculative[2].frame = 78;
    speculative[2].flags =
        Horse::kRollbackProductionSummaryConfirmed;
    speculative[3] = summary;
    speculative[3].frame = 79;
    speculative_valid[2] = true;
    speculative_valid[3] = true;
    const auto before_rejected_invalidation = speculative;
    const auto valid_before_rejected_invalidation = speculative_valid;
    if (Horse::CanInvalidateRollbackProductionSummariesAfter(
            speculative, speculative_valid, 77)
        || Horse::InvalidateRollbackProductionSummariesAfter(
            speculative, speculative_valid, 77)
        || speculative_valid != valid_before_rejected_invalidation
        || std::memcmp(speculative.data(),
            before_rejected_invalidation.data(),
            sizeof(speculative)) != 0)
    {
        std::cerr << "confirmed summary Load invalidation was not atomic\n";
        return 1;
    }

    using Summary = Horse::RollbackProductionFrameSummary;
    const std::vector<void (*)(Summary&)> mutations {
        [](Summary& value) { value.epoch ^= 1; },
        [](Summary& value) { value.frame ^= 1; },
        [](Summary& value) { value.canonical_hash ^= 1; },
        [](Summary& value) { value.component_hash[2] ^= 1; },
        [](Summary& value) { value.hgcpu_peer.chara_stream_hash[0] ^= 1; },
        [](Summary& value) { value.hgcpu_peer.chara_chunk_hash[1][0] ^= 1; },
        [](Summary& value) { value.hgcpu_peer.motion_slot_hash ^= 1; },
        [](Summary& value) { value.hgcpu_peer.motion_provider_age[0][0] ^= 1; },
        [](Summary& value) { value.explicit_range_hash[0] ^= 1; },
        [](Summary& value) { value.explicit_range_hash[1] ^= 1; },
        [](Summary& value) { value.explicit_range_hash[2] ^= 1; },
        [](Summary& value) { value.explicit_range_hash[3] ^= 1; },
        [](Summary& value) { value.explicit_range_count = 1; },
        [](Summary& value) { value.ccpu_canonical_hash ^= 1; },
        [](Summary& value) { value.native_round_state_hash ^= 1; },
        [](Summary& value) { value.native_simulation_state_hash ^= 1; },
        [](Summary& value) {
            value.palette_variant_canonical_hash ^= 1;
        },
        [](Summary& value) {
            value.palette_variant_active_mask ^= 1;
        },
        [](Summary& value) { value.terminal_round_result ^= 1; },
        [](Summary& value) { value.round_result_type ^= 1; },
        [](Summary& value) { value.input[0] ^= 1; },
        [](Summary& value) { value.input[1] ^= 1; },
    };
    for (size_t i = 0; i < mutations.size(); ++i)
    {
        if (!rejects_mutation(summary, mutations[i]))
        {
            std::cerr << "production summary mutation accepted: " << i
                      << '\n';
            return 1;
        }
    }

    auto transport_metadata = summary;
    transport_metadata.ack_valid = 1;
    transport_metadata.ack_next_unmatched_frame = summary.frame + 9;
    transport_metadata.ack_selective[0] = 0x55AA55AA55AA55AAull;
    transport_metadata.ack_selective[1] = 0x8000000000000001ull;
    if (!Horse::RollbackProductionSummaryStateMatches(
            summary, transport_metadata))
    {
        std::cerr << "transport ACK metadata became gameplay-canonical\n";
        return 1;
    }

    Horse::RollbackProductionSummaryAckWindow ack_window {};
    ack_window.epoch = summary.epoch;
    ack_window.next_unmatched_frame = summary.frame + 1;
    ack_window.selective[0] = 5;
    ack_window.next_missing_frame = summary.frame + 3;
    ack_window.received_selective[0] = 9;
    if (!Horse::RollbackProductionSummaryAckWindowValid(
            ack_window, summary.epoch))
    {
        std::cerr << "valid cumulative ACK window rejected\n";
        return 1;
    }
    auto invalid_ack_window = ack_window;
    invalid_ack_window.version ^= 1;
    if (Horse::RollbackProductionSummaryAckWindowValid(
            invalid_ack_window, summary.epoch))
    {
        std::cerr << "wrong cumulative ACK version accepted\n";
        return 1;
    }
    invalid_ack_window = ack_window;
    invalid_ack_window.reserved = 1;
    if (Horse::RollbackProductionSummaryAckWindowValid(
            invalid_ack_window, summary.epoch))
    {
        std::cerr << "nonzero cumulative ACK reserved field accepted\n";
        return 1;
    }
    invalid_ack_window = ack_window;
    invalid_ack_window.reserved3 = 1;
    if (Horse::RollbackProductionSummaryAckWindowValid(
            invalid_ack_window, summary.epoch))
    {
        std::cerr << "nonzero receipt ACK reserved field accepted\n";
        return 1;
    }
    if (Horse::RollbackProductionSummaryAckWindowValid(
            ack_window, summary.epoch + 1))
    {
        std::cerr << "cross-epoch cumulative ACK accepted\n";
        return 1;
    }
    if (Horse::RollbackProductionSummaryResendBudget(0) != 2
        || Horse::RollbackProductionSummaryResendBudget(7) != 2
        || Horse::RollbackProductionSummaryResendBudget(8) != 4
        || Horse::RollbackProductionSummaryResendBudget(128) != 4)
    {
        std::cerr << "adaptive summary resend budget is invalid\n";
        return 1;
    }
    if (Horse::RollbackProductionSummaryBackpressureRequired(
            119, true, 0)
        || !Horse::RollbackProductionSummaryBackpressureRequired(
            120, true, 0)
        || !Horse::RollbackProductionSummaryBackpressureRequired(
            247, true, 127)
        || Horse::RollbackProductionSummaryBackpressureRequired(
            1000, false, 0)
        || Horse::RollbackProductionSummaryBackpressureRequired(
            99, true, 100))
    {
        std::cerr << "summary consensus backpressure boundary is invalid\n";
        return 1;
    }

    auto shifted_snapshot = make_snapshot();
    const uint32_t shifted_current = 3424;
    const uint32_t shifted_last = 3422;
    std::memcpy(shifted_snapshot.bytes.data() + 0xC0,
        &shifted_current, sizeof(shifted_current));
    std::memcpy(shifted_snapshot.bytes.data() + 0xC4,
        &shifted_last, sizeof(shifted_last));
    auto shifted_summary = make_summary();
    const auto shifted_detail =
        Horse::BuildRollbackProductionSummaryDetails(
            shifted_snapshot, shifted_summary);
    if (!shifted_detail.ok
        || !Horse::RollbackProductionSummaryStateMatches(
            summary, shifted_summary))
    {
        std::cerr << "equivalent cooldown rejected by production summary\n";
        return 1;
    }
    auto wrapped_snapshot = make_snapshot();
    const uint32_t wrapped_current = 0xFFFFFFFFu;
    const uint32_t wrapped_last = 0xFFFFFFFCu;
    std::memcpy(wrapped_snapshot.bytes.data() + 0xC0,
        &wrapped_current, sizeof(wrapped_current));
    std::memcpy(wrapped_snapshot.bytes.data() + 0xC4,
        &wrapped_last, sizeof(wrapped_last));
    auto wrapped_summary = make_summary();
    const auto wrapped_detail =
        Horse::BuildRollbackProductionSummaryDetails(
            wrapped_snapshot, wrapped_summary);
    auto ordinary_remaining_one_snapshot = make_snapshot();
    const uint32_t ordinary_current = 103;
    const uint32_t ordinary_last = 100;
    std::memcpy(ordinary_remaining_one_snapshot.bytes.data() + 0xC0,
        &ordinary_current, sizeof(ordinary_current));
    std::memcpy(ordinary_remaining_one_snapshot.bytes.data() + 0xC4,
        &ordinary_last, sizeof(ordinary_last));
    auto ordinary_remaining_one_summary = make_summary();
    const auto ordinary_remaining_one_detail =
        Horse::BuildRollbackProductionSummaryDetails(
            ordinary_remaining_one_snapshot,
            ordinary_remaining_one_summary);
    if (!wrapped_detail.ok
        || !ordinary_remaining_one_detail.ok
        || !Horse::RollbackProductionSummaryStateMatches(
            ordinary_remaining_one_summary, wrapped_summary))
    {
        std::cerr << "equivalent wrapped cooldown rejected by production summary\n";
        return 1;
    }
    const uint32_t different_remaining = 3423;
    std::memcpy(shifted_snapshot.bytes.data() + 0xC4,
        &different_remaining, sizeof(different_remaining));
    shifted_summary = make_summary();
    if (!Horse::BuildRollbackProductionSummaryDetails(
            shifted_snapshot, shifted_summary).ok
        || Horse::RollbackProductionSummaryStateMatches(
            summary, shifted_summary))
    {
        std::cerr << "different cooldown accepted by production summary\n";
        return 1;
    }
    shifted_snapshot = make_snapshot();
    const uint32_t different_owner = 2;
    std::memcpy(shifted_snapshot.bytes.data() + 0xC8,
        &different_owner, sizeof(different_owner));
    shifted_summary = make_summary();
    if (!Horse::BuildRollbackProductionSummaryDetails(
            shifted_snapshot, shifted_summary).ok
        || Horse::RollbackProductionSummaryStateMatches(
            summary, shifted_summary))
    {
        std::cerr << "different cooldown owner accepted by production summary\n";
        return 1;
    }

    auto local_provider_image = summary;
    local_provider_image.hgcpu_peer.motion_provider_hash[0][0] ^= 1;
    local_provider_image.hgcpu_peer.reserved[0] ^= 1;
    if (!Horse::RollbackProductionSummaryStateMatches(
            summary, local_provider_image)
        || Horse::RollbackProductionSummaryHgCpuPeerHash(summary)
            != Horse::RollbackProductionSummaryHgCpuPeerHash(
                local_provider_image))
    {
        std::cerr << "local motion-provider diagnostic became canonical\n";
        return 1;
    }

    auto missing_ccpu = make_snapshot();
    missing_ccpu.ranges[0].canonical_policy =
        Horse::RollbackCanonicalPolicy::AllBytes;
    auto candidate = make_summary();
    if (Horse::BuildRollbackProductionSummaryDetails(
            missing_ccpu, candidate).ok)
    {
        std::cerr << "missing CCPU range accepted\n";
        return 1;
    }

    auto duplicate_ccpu = make_snapshot();
    duplicate_ccpu.bytes.resize(0x18C);
    duplicate_ccpu.ranges[1] = {
        1, 0x2000, 0xC0, 0xC0, 0,
        Horse::RollbackCanonicalPolicy::LuxMoveSchedStateArray};
    candidate = make_summary();
    if (Horse::BuildRollbackProductionSummaryDetails(
            duplicate_ccpu, candidate).ok)
    {
        std::cerr << "duplicate CCPU range accepted\n";
        return 1;
    }

    auto too_many = make_snapshot();
    too_many.ranges.resize(33, too_many.ranges[1]);
    candidate = make_summary();
    if (Horse::BuildRollbackProductionSummaryDetails(
            too_many, candidate).ok)
    {
        std::cerr << "over-capacity explicit ranges accepted\n";
        return 1;
    }

    Horse::RollbackSnapshotFrame empty {};
    candidate = make_summary();
    if (Horse::BuildRollbackProductionSummaryDetails(empty, candidate).ok)
    {
        std::cerr << "empty explicit ranges accepted\n";
        return 1;
    }

    std::cout << "rollback production summary selftest: PASS\n";
    return 0;
}
