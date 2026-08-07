#include "RollbackLaunchContract.hpp"
#include "RollbackStateHash.hpp"
#include "RollbackStockOnlineLabDriver.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>

int main()
{
    using namespace Horse;

    const std::array<uint8_t, 19> hash_fixture {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16, 17, 18, 19,
    };
    RollbackFastHash chunked_hash {};
    chunked_hash.add_bytes(hash_fixture.data(), 3);
    chunked_hash.add_bytes(hash_fixture.data() + 3, 9);
    chunked_hash.add_bytes(hash_fixture.data() + 12, 7);
    RollbackFastHash zero_hash {};
    zero_hash.add_bytes(hash_fixture.data(), 3);
    zero_hash.add_zero_bytes(16);
    std::array<uint8_t, 19> explicit_zero {};
    std::memcpy(explicit_zero.data(), hash_fixture.data(), 3);
    std::array<uint8_t, 257> large_hash_fixture {};
    for (size_t i = 0; i < large_hash_fixture.size(); ++i)
        large_hash_fixture[i] = static_cast<uint8_t>(i * 37u + 11u);
    RollbackFastHash chunked_large_hash {};
    chunked_large_hash.add_bytes(large_hash_fixture.data(), 5);
    chunked_large_hash.add_bytes(large_hash_fixture.data() + 5, 73);
    chunked_large_hash.add_bytes(large_hash_fixture.data() + 78, 129);
    chunked_large_hash.add_bytes(
        large_hash_fixture.data() + 207,
        large_hash_fixture.size() - 207);
    const bool fast_hash_ok = chunked_hash.finish()
            == RollbackFastIntegrityHashBytes(
                hash_fixture.data(), hash_fixture.size())
        && zero_hash.finish() == RollbackFastIntegrityHashBytes(
            explicit_zero.data(), explicit_zero.size())
        && chunked_large_hash.finish()
            == RollbackFastIntegrityHashBytes(
                large_hash_fixture.data(), large_hash_fixture.size());

    RollbackLaunchBarrierMessage local {};
    local.stage = RollbackLaunchBarrierStage::BattleBaseline;
    local.local_player_slot = 0;
    local.logical_frame = 0;
    local.native_boundary_frame = 120;
    local.round_ordinal = 2;
    local.input_log_frame = 120;
    local.completed_round_ordinal = 1;
    local.replay_round_index = 2;
    local.canonical_stage_identity = 0x10003;
    local.session_epoch = 0x10101010;
    local.round_generation = 3;
    local.match_identity_digest = 0x20202020;
    local.replay_digest = 0x30303030;
    local.round_identity_digest = 0x40404040;
    local.lifecycle_digest = 0x1111222233334444ull;
    local.epoch = 0x5555666677778888ull;
    local.precontrol_identity_digest = 0x8182838485868788ull;
    local.canonical_baseline_hash = 0x9999AAAABBBBCCCCull;
    local.component_hash[0] = 0x11;
    local.component_hash[1] = 0x22;
    local.component_hash[2] = 0x33;
    local.component_hash[3] = 0x44;
    RollbackLaunchBarrierMessage peer = local;
    peer.local_player_slot = 1;
    RollbackLaunchBarrierMessage accepted = peer;
    accepted.stage = RollbackLaunchBarrierStage::BattleBaselineAccepted;
    RollbackLaunchBarrierMessage wrong_slot = peer;
    wrong_slot.local_player_slot = 0;
    RollbackLaunchBarrierMessage wrong_hash = peer;
    ++wrong_hash.canonical_baseline_hash;
    RollbackLaunchBarrierMessage wrong_stage = peer;
    ++wrong_stage.canonical_stage_identity;
    RollbackLaunchBarrierMessage missing_stage = peer;
    missing_stage.canonical_stage_identity = 0;
    RollbackLaunchBarrierMessage stock_input_skew = peer;
    ++stock_input_skew.input_log_frame;
    RollbackLaunchBarrierMessage native_frame_skew = peer;
    native_frame_skew.native_boundary_frame += 37;
    constexpr uint64_t launch_generation = 0x4242;
    RollbackLaunchBarrierInbox remote_first {};
    const bool remote_first_configured = remote_first.configure(
        0, local.session_epoch, launch_generation);
    const auto remote_first_store = remote_first.accept_peer(
        peer, launch_generation);
    const auto remote_first_duplicate = remote_first.accept_peer(
        peer, launch_generation);
    const auto remote_first_conflict = remote_first.accept_peer(
        wrong_hash, launch_generation);
    const bool remote_first_not_ready = remote_first.peer_valid()
        && !remote_first.local_valid()
        && !remote_first.ready(
            RollbackLaunchBarrierStage::BattleBaseline);
    const auto remote_first_local = remote_first.observe_local(
        local, launch_generation);
    const bool remote_first_baseline_ready = remote_first.ready(
        RollbackLaunchBarrierStage::BattleBaseline)
        && !remote_first.ready(
            RollbackLaunchBarrierStage::BattleBaselineAccepted);
    RollbackLaunchBarrierMessage accepted_local = local;
    accepted_local.stage =
        RollbackLaunchBarrierStage::BattleBaselineAccepted;
    const auto accepted_local_store = remote_first.observe_local(
        accepted_local, launch_generation);
    const bool accepted_local_waits_for_peer = !remote_first.ready(
        RollbackLaunchBarrierStage::BattleBaselineAccepted);
    const auto accepted_peer_store = remote_first.accept_peer(
        accepted, launch_generation);
    const bool accepted_pair_ready = remote_first.ready(
        RollbackLaunchBarrierStage::BattleBaselineAccepted);
    const auto stale_peer_baseline = remote_first.accept_peer(
        peer, launch_generation);

    RollbackLaunchBarrierInbox mismatching_pair {};
    const bool mismatch_order_ok = mismatching_pair.configure(
            0, local.session_epoch, launch_generation)
        && mismatching_pair.accept_peer(peer, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Stored
        && mismatching_pair.observe_local(
                wrong_hash, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Invalid;
    // wrong_hash belongs to peer slot 1; use a local-slot baseline with the
    // same conflicting canonical hash to exercise bilateral mismatch.
    RollbackLaunchBarrierMessage wrong_local_hash = local;
    ++wrong_local_hash.canonical_baseline_hash;
    RollbackLaunchBarrierInbox bilateral_mismatch {};
    const bool bilateral_mismatch_ok = bilateral_mismatch.configure(
            0, local.session_epoch, launch_generation)
        && bilateral_mismatch.accept_peer(peer, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Stored
        && bilateral_mismatch.observe_local(
                wrong_local_hash, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Stored
        && !bilateral_mismatch.ready(
            RollbackLaunchBarrierStage::BattleBaseline)
        && !bilateral_mismatch.ready(
            RollbackLaunchBarrierStage::BattleBaselineAccepted);
    RollbackLaunchBarrierMessage wrong_session = peer;
    ++wrong_session.session_epoch;
    const bool invalid_envelope_ok =
        remote_first.accept_peer(peer, launch_generation + 1)
            == RollbackLaunchBarrierInboxDisposition::Invalid
        && remote_first.accept_peer(wrong_session, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Invalid
        && remote_first.accept_peer(wrong_slot, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Invalid;
    const bool launch_inbox_ok = remote_first_configured
        && remote_first_store
            == RollbackLaunchBarrierInboxDisposition::Stored
        && remote_first_duplicate
            == RollbackLaunchBarrierInboxDisposition::Idempotent
        && remote_first_conflict
            == RollbackLaunchBarrierInboxDisposition::Conflict
        && remote_first_not_ready
        && remote_first_local
            == RollbackLaunchBarrierInboxDisposition::Stored
        && remote_first_baseline_ready
        && accepted_local_store
            == RollbackLaunchBarrierInboxDisposition::Stored
        && accepted_local_waits_for_peer
        && accepted_peer_store
            == RollbackLaunchBarrierInboxDisposition::Stored
        && accepted_pair_ready
        && stale_peer_baseline
            == RollbackLaunchBarrierInboxDisposition::Stale
        && mismatch_order_ok && bilateral_mismatch_ok
        && invalid_envelope_ok;

    // The peer's Baseline packet may be lost while its later Accepted packet
    // arrives first. Accepted carries the same immutable baseline payload and
    // therefore satisfies the lower baseline prerequisite without requiring
    // the stale lower-stage packet to arrive.
    RollbackLaunchBarrierInbox accepted_peer_first {};
    const bool accepted_peer_first_ok = accepted_peer_first.configure(
            0, local.session_epoch, launch_generation)
        && accepted_peer_first.accept_peer(accepted, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Stored
        && !accepted_peer_first.ready(
            RollbackLaunchBarrierStage::BattleBaseline)
        && accepted_peer_first.observe_local(local, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Stored
        && accepted_peer_first.ready(
            RollbackLaunchBarrierStage::BattleBaseline)
        && !accepted_peer_first.ready(
            RollbackLaunchBarrierStage::BattleBaselineAccepted)
        && accepted_peer_first.observe_local(
                accepted_local, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Stored
        && accepted_peer_first.ready(
            RollbackLaunchBarrierStage::BattleBaselineAccepted)
        && accepted_peer_first.accept_peer(peer, launch_generation)
            == RollbackLaunchBarrierInboxDisposition::Stale;
    RollbackRoundTransitionBarrierMessage transition_local {};
    transition_local.stage =
        RollbackRoundTransitionBarrierStage::Proposed;
    transition_local.local_player_slot = 0;
    transition_local.confirmed_frame = 2337;
    transition_local.tail_start_frame = 2290;
    transition_local.completed_round_ordinal = 0;
    transition_local.pair_epoch = 0x12345678;
    transition_local.canonical_hash = 0xABCDEF01;
    RollbackRoundTransitionBarrierMessage transition_peer =
        transition_local;
    transition_peer.local_player_slot = 1;
    transition_peer.tail_start_frame = 2312;
    auto transition_local_restored = transition_local;
    transition_local_restored.stage =
        RollbackRoundTransitionBarrierStage::Restored;
    auto transition_peer_restored = transition_peer;
    transition_peer_restored.stage =
        RollbackRoundTransitionBarrierStage::Restored;
    RollbackRoundTransitionBarrierMessage transition_local_accepted =
        transition_local;
    transition_local_accepted.stage =
        RollbackRoundTransitionBarrierStage::Accepted;
    RollbackRoundTransitionBarrierMessage transition_peer_accepted =
        transition_peer;
    transition_peer_accepted.stage =
        RollbackRoundTransitionBarrierStage::Accepted;
    RollbackRoundTransitionBarrierMessage transition_wrong_frame =
        transition_peer;
    ++transition_wrong_frame.confirmed_frame;
    transition_wrong_frame.canonical_hash = 0xABCDEF02;
    auto transition_wrong_hash = transition_peer;
    transition_wrong_hash.canonical_hash = 0xABCDEF02;
    auto transition_invalid_tail = transition_peer;
    transition_invalid_tail.tail_start_frame =
        transition_invalid_tail.confirmed_frame + 1;
    auto transition_later = transition_peer;
    transition_later.confirmed_frame += 8;
    transition_later.canonical_hash = 0x1234ABCD;
    auto transition_local_later = transition_local;
    transition_local_later.confirmed_frame = transition_later.confirmed_frame;
    transition_local_later.canonical_hash = transition_later.canonical_hash;
    uint32_t transition_tail_start = 0;
    const bool transition_barrier_ok =
        RollbackRoundTransitionBarriersMatch(
            transition_local, transition_peer)
        && RollbackRoundTransitionTailStart(
            transition_local, transition_peer, transition_tail_start)
        && transition_tail_start == transition_local.tail_start_frame
        && !RollbackRoundTransitionBarrierValid(transition_invalid_tail)
        && ClassifyRollbackRoundTerminalProposal(
            transition_local, transition_peer, 2337, true)
                == RollbackRoundTerminalProposalAction::Agreed
        && ClassifyRollbackRoundTerminalProposal(
            transition_local, transition_later, 2344, false)
                == RollbackRoundTerminalProposalAction::WaitForLocalFrame
        && ClassifyRollbackRoundTerminalProposal(
            transition_local, transition_later, 2345, false)
                == RollbackRoundTerminalProposalAction::WaitForLocalFrame
        && ClassifyRollbackRoundTerminalProposal(
            transition_local, transition_later, 2345, true)
                == RollbackRoundTerminalProposalAction::AdoptRemote
        && ClassifyRollbackRoundTerminalProposal(
            transition_local_later, transition_peer, 2345, true)
                == RollbackRoundTerminalProposalAction::KeepLocal
        && ClassifyRollbackRoundTerminalProposal(
            transition_local, transition_peer_restored, 2337, true)
                == RollbackRoundTerminalProposalAction::Agreed
        && RollbackRoundTransitionBarrierIdentityEqual(
            transition_peer, transition_peer_restored)
        && !RollbackRoundTransitionBarrierIdentityEqual(
            transition_local, transition_peer)
        && !RollbackRoundTransitionBarrierComplete(
            transition_local, transition_peer)
        && !RollbackRoundTransitionBarrierComplete(
            transition_local_restored, transition_peer_restored)
        && RollbackRoundTransitionBarrierComplete(
            transition_local_accepted, transition_peer_accepted)
        && !RollbackRoundTransitionBarriersMatch(
            transition_local, transition_wrong_frame)
        && ClassifyRollbackRoundTerminalProposal(
            transition_local, transition_wrong_hash, 2337, true)
                == RollbackRoundTerminalProposalAction::Reject;

    // Losing the first Accepted datagram must not strand the other peer while
    // the gameplay coordinate is frozen. Retransmission uses the independent
    // service tick and remains armed after this peer reaches completion.
    const bool terminal_accepted_republish_ok =
        !RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_restored, true, true, false, 6)
        && !RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_accepted, false, true, false, 6)
        && !RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_accepted, true, false, false, 6)
        && !RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_accepted, true, true, false, 5)
        && RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_accepted, true, true, false, 6)
        && RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_accepted, true, true, false, 12)
        && !RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_accepted, true, true, true, 12)
        && !RollbackRoundTransitionAcceptedRepublishDue(
            transition_local_accepted, true, true, false, 12, 0);

    // A peer can observe the native terminal edge several logical frames
    // earlier. Neither peer restores the early frame: the earlier proposal
    // waits until its confirmed frontier has retained the later peer's exact
    // checkpoint, then adopts that later identity.
    auto early_terminal = transition_local;
    early_terminal.confirmed_frame = 1910;
    early_terminal.tail_start_frame = 1888;
    early_terminal.canonical_hash = 0x1910;
    auto late_terminal = transition_peer;
    late_terminal.confirmed_frame = 1918;
    late_terminal.tail_start_frame = 1901;
    late_terminal.canonical_hash = 0x1918;
    auto adopted_terminal = early_terminal;
    adopted_terminal.confirmed_frame = late_terminal.confirmed_frame;
    adopted_terminal.canonical_hash = late_terminal.canonical_hash;
    const bool asymmetric_terminal_proposal_ok =
        ClassifyRollbackRoundTerminalProposal(
            early_terminal, late_terminal, 1917, false)
                == RollbackRoundTerminalProposalAction::WaitForLocalFrame
        && ClassifyRollbackRoundTerminalProposal(
            early_terminal, late_terminal, 1918, true)
                == RollbackRoundTerminalProposalAction::AdoptRemote
        && ClassifyRollbackRoundTerminalProposal(
            late_terminal, early_terminal, 1918, true)
                == RollbackRoundTerminalProposalAction::KeepLocal
        && RollbackRoundTransitionBarriersMatch(
            adopted_terminal, late_terminal)
        && ClassifyRollbackRoundTerminalProposal(
            adopted_terminal, late_terminal, 1918, true)
                == RollbackRoundTerminalProposalAction::Agreed;
    RollbackPreNewRoundBarrierMessage pre_round_local {};
    pre_round_local.stage = RollbackPreNewRoundBarrierStage::Ready;
    pre_round_local.local_player_slot = 0;
    pre_round_local.completed_round_ordinal = 1;
    pre_round_local.target_round_ordinal = 2;
    pre_round_local.session_epoch = 0x100;
    pre_round_local.completed_pair_epoch = 0x200;
    pre_round_local.terminal_canonical_hash = 0x300;
    pre_round_local.target_round_generation = 2;
    pre_round_local.match_identity_digest = 0x400;
    pre_round_local.entry_digest = 0x500;
    pre_round_local.native_stage_identity = 0x10003;
    RollbackPreNewRoundBarrierMessage pre_round_peer = pre_round_local;
    pre_round_peer.local_player_slot = 1;
    auto pre_round_local_accepted = pre_round_local;
    auto pre_round_peer_accepted = pre_round_peer;
    pre_round_local_accepted.stage =
        RollbackPreNewRoundBarrierStage::Accepted;
    pre_round_peer_accepted.stage =
        RollbackPreNewRoundBarrierStage::Accepted;
    auto pre_round_wrong_epoch = pre_round_peer;
    ++pre_round_wrong_epoch.completed_pair_epoch;
    const bool pre_round_barrier_ok =
        RollbackPreNewRoundBarrierValid(pre_round_local)
        && RollbackPreNewRoundBarrierSamePeerIdentity(
            pre_round_local, pre_round_local_accepted)
        && !RollbackPreNewRoundBarrierSamePeerIdentity(
            pre_round_local, pre_round_peer)
        && RollbackPreNewRoundBarriersMatch(
            pre_round_local, pre_round_peer)
        && !RollbackPreNewRoundBarrierComplete(
            pre_round_local, pre_round_peer)
        && RollbackPreNewRoundBarrierComplete(
            pre_round_local_accepted, pre_round_peer_accepted)
        && !RollbackPreNewRoundBarriersMatch(
            pre_round_local, pre_round_wrong_epoch);

    RollbackSteamSessionIdentity steam_identity {};
    steam_identity.lobby_id = 0x1001;
    steam_identity.owner_steam_id = 0x2001;
    steam_identity.local_steam_id = 0x2001;
    steam_identity.remote_steam_id = 0x2002;
    steam_identity.selection_hash = 0x3001;
    steam_identity.lifecycle_serial = 7;
    steam_identity.named_session = 0x4100;
    steam_identity.session_info = 0x4200;
    steam_identity.connect_manager = 0x4300;
    steam_identity.active_connect = 0x4400;
    steam_identity.active_transport = 0x44A8;
    steam_identity.session_connection = 0x4500;
    steam_identity.active_connect_state = 3;
    steam_identity.active_connect_sub_state = 0;
    steam_identity.named_session_valid = true;
    steam_identity.transport_connected = true;
    RollbackSteamSessionIdentity changed_lobby = steam_identity;
    ++changed_lobby.lobby_id;
    RollbackSteamSessionIdentity transient_session = steam_identity;
    transient_session.named_session_valid = false;
    RollbackSteamSessionIdentity changed_epoch = steam_identity;
    changed_epoch.session_connection += 0x100;
    RollbackSteamSessionIdentity terminal_after_pointer_clear =
        steam_identity;
    terminal_after_pointer_clear.active_transport = 0;
    terminal_after_pointer_clear.session_connection = 0;
    terminal_after_pointer_clear.transport_connected = false;
    terminal_after_pointer_clear.native_terminal = true;
    terminal_after_pointer_clear.terminal_evidence =
        RollbackSteamNativeTerminalEvidence::LuxorRetryExhausted;
    terminal_after_pointer_clear.active_connect_state = 5;
    terminal_after_pointer_clear.active_connect_sub_state = 9;
    RollbackSteamSessionIdentity reused_pointers_new_lifecycle =
        steam_identity;
    ++reused_pointers_new_lifecycle.lifecycle_serial;
    RollbackSteamSessionIdentity incomplete_initial = steam_identity;
    incomplete_initial.session_connection = 0;
    incomplete_initial.transport_connected = false;
    RollbackSteamP2PConnectFailObservation connect_fail {};
    connect_fail.sequence = 11;
    connect_fail.lifecycle_serial = steam_identity.lifecycle_serial;
    connect_fail.remote_steam_id = steam_identity.remote_steam_id;
    connect_fail.error = 4;
    auto stale_connect_fail = connect_fail;
    --stale_connect_fail.lifecycle_serial;
    auto other_remote_connect_fail = connect_fail;
    ++other_remote_connect_fail.remote_steam_id;
    auto overflow_connect_fail = connect_fail;
    overflow_connect_fail.sequence = 0;
    overflow_connect_fail.lifecycle_serial = 0;
    overflow_connect_fail.remote_steam_id = 0;
    overflow_connect_fail.stream_overflow = true;
    const bool connect_fail_observation_ok =
        ClassifyRollbackSteamP2PConnectFailObservation(
            connect_fail, steam_identity.lifecycle_serial,
            steam_identity.remote_steam_id)
            == RollbackSteamP2PConnectFailDisposition::Terminal
        && ClassifyRollbackSteamP2PConnectFailObservation(
            stale_connect_fail, steam_identity.lifecycle_serial,
            steam_identity.remote_steam_id)
            == RollbackSteamP2PConnectFailDisposition::StaleLifecycle
        && ClassifyRollbackSteamP2PConnectFailObservation(
            other_remote_connect_fail, steam_identity.lifecycle_serial,
            steam_identity.remote_steam_id)
            == RollbackSteamP2PConnectFailDisposition::OtherRemote
        && ClassifyRollbackSteamP2PConnectFailObservation(
            overflow_connect_fail, steam_identity.lifecycle_serial,
            steam_identity.remote_steam_id)
            == RollbackSteamP2PConnectFailDisposition::
                TerminalStreamOverflow
        && ClassifyRollbackSteamP2PConnectFailObservation(
            connect_fail, 0, steam_identity.remote_steam_id)
            == RollbackSteamP2PConnectFailDisposition::Invalid;
    RollbackSessionContractMessage contract_host {};
    contract_host.stage = RollbackSessionContractStage::Accepted;
    contract_host.local_player_slot = 0;
    contract_host.local_is_host = 1;
    contract_host.rollback_window = 12;
    contract_host.input_delay = 1;
    contract_host.lobby_id = steam_identity.lobby_id;
    contract_host.owner_steam_id = steam_identity.owner_steam_id;
    contract_host.local_steam_id = steam_identity.local_steam_id;
    contract_host.remote_steam_id = steam_identity.remote_steam_id;
    contract_host.selection_hash = steam_identity.selection_hash;
    contract_host.host_nonce = 0x4001;
    contract_host.build_id = 0x5001;
    contract_host.schema_id = 0x6001;
    contract_host.contract_hash = 0x7001;
    contract_host.session_epoch = 0x8001;
    RollbackSessionContractMessage contract_guest = contract_host;
    contract_guest.local_player_slot = 1;
    contract_guest.local_is_host = 0;
    contract_guest.local_steam_id = contract_host.remote_steam_id;
    contract_guest.remote_steam_id = contract_host.local_steam_id;
    RollbackSessionContractMessage stale_contract = contract_guest;
    ++stale_contract.session_epoch;
    RollbackSessionContractMessage mismatched_contract = contract_guest;
    ++mismatched_contract.contract_hash;
    RollbackSessionContractMessage duplicate_slot = contract_guest;
    duplicate_slot.local_player_slot = 0;

    RollbackDeterministicInputConfig fixture {};
    fixture.enabled = true;
    RollbackDeterministicInputConfig bad_fixture = fixture;
    bad_fixture.delay_owner_slot = 1;
    RollbackReplayInputConfig replay_input {};
    replay_input.enabled = true;
    replay_input.file_sha256[0] = 1;
    replay_input.round_index = 2;
    replay_input.start_frame = 10;
    replay_input.round_frame_count = 1000;
    replay_input.replay_random_seed = 0x12345678;
    replay_input.round_start_count = 5;
    replay_input.round_start_frames = {2, 0, 10, 1, 3};
    replay_input.round_input_hash = {0x1111, 0x2222};
    uint32_t replay_index = 0;
    uint16_t prediction_lead = 0;
    RollbackReplayInputConfig swapped_replay = replay_input;
    swapped_replay.swap_players = true;
    RollbackReplayInputConfig different_round_origin = replay_input;
    different_round_origin.round_start_frames[3] = 2;
    RollbackReplayInputConfig missing_seed = replay_input;
    missing_seed.replay_random_seed = 0;
    uint32_t consumed_index = 0;
    uint32_t neutral_tail_index = 0;
    bool neutral_tail_authored = false;
    uint32_t delayed_submission_index = 0;
    uint32_t round_start_index = 0;
    uint32_t next_replay_round = 0;
    bool replay_alignment_matrix_ok = true;
    for (uint16_t delay : {uint16_t {0}, uint16_t {1}, uint16_t {3}})
    {
        for (uint16_t depth : {uint16_t {2}, uint16_t {6}, uint16_t {9}})
        {
            uint32_t consumed = 0;
            uint32_t submitted = 0;
            replay_alignment_matrix_ok = replay_alignment_matrix_ok
                && depth + delay <= 12
                && RollbackConsumedReplayIndex(
                    10, 1000, 120, delay, consumed)
                && consumed == 130
                && RollbackSubmittedReplayIndex(
                    10, 1000, 120, delay, submitted)
                && submitted == 130u + delay;
        }
    }
    RollbackFixtureBarrierMessage fixture_local {};
    fixture_local.local_player_slot = 0;
    fixture_local.fixture_id = fixture.fixture_id;
    fixture_local.submission_frame = fixture.correction_start;
    fixture_local.pair_epoch = 0x12345678u;
    RollbackFixtureBarrierMessage fixture_peer = fixture_local;
    fixture_peer.local_player_slot = 1;
    RollbackFixtureBarrierMessage fixture_wrong_frame = fixture_peer;
    ++fixture_wrong_frame.submission_frame;
    const bool fixture_ok = fixture.valid()
        && !bad_fixture.valid()
        && fixture.prediction_lead_updates == 12
        && RollbackFixtureLastHeldFrame(fixture) == 125
        && RollbackFixtureEvidenceStartFrame(fixture) == 120
        && RollbackFixtureEvidenceEndFrame(fixture) == 140
        && RollbackFixtureCheckpointFrame(fixture) == 150
        && RollbackFixtureEvidenceFrame(fixture, 120)
        && RollbackFixtureEvidenceFrame(fixture, 140)
        && !RollbackFixtureEvidenceFrame(fixture, 119)
        && !RollbackFixtureEvidenceFrame(fixture, 141)
        && RollbackFixtureLoadAffectsEvidence(fixture, 1, 119)
        && RollbackFixtureLoadAffectsEvidence(fixture, 1, 120)
        && !RollbackFixtureLoadAffectsEvidence(fixture, 1, 118)
        && !RollbackFixtureLoadAffectsEvidence(fixture, 1, 121)
        && replay_input.resolved_valid()
        && replay_input.replay_player_for_native_slot(0) == 0
        && swapped_replay.replay_player_for_native_slot(0) == 1
        && replay_input.input_index(120, 3, replay_index)
        && replay_index == 133
        && replay_input.consumed_index(120, 3, consumed_index)
        && consumed_index == 130
        && RollbackSubmittedReplayIndex(
            10, 1000, 120, 3, delayed_submission_index)
        && delayed_submission_index == 133
        && RollbackConsumedReplayIndex(
            10, 1000, 120, 0, consumed_index)
        && consumed_index == 130
        && RollbackConsumedReplayIndex(
            10, 1000, 120, 1, consumed_index)
        && consumed_index == 130
        && RollbackConsumedReplayIndex(
            10, 1000, 120, 3, consumed_index)
        && consumed_index == 130
        && replay_input.consumed_index_or_neutral_tail(
            989, 3, neutral_tail_index, neutral_tail_authored)
        && neutral_tail_index == 999
        && neutral_tail_authored
        && replay_input.consumed_index_or_neutral_tail(
            990, 3, neutral_tail_index, neutral_tail_authored)
        && neutral_tail_index == 1000
        && !neutral_tail_authored
        && RollbackConsumedReplayIndexOrNeutralTail(
            10, 1000, 2000, 0,
            neutral_tail_index, neutral_tail_authored)
        && neutral_tail_index == 2010
        && !neutral_tail_authored
        && !RollbackConsumedReplayIndexOrNeutralTail(
            UINT32_MAX, 1000, 1, 0,
            neutral_tail_index, neutral_tail_authored)
        && !RollbackConsumedReplayIndexOrNeutralTail(
            0, 0, 0, 0,
            neutral_tail_index, neutral_tail_authored)
        && replay_input.hash() != swapped_replay.hash()
        && replay_input.hash() != different_round_origin.hash()
        && !missing_seed.requested_valid()
        && replay_input.start_frame_for_round(3, round_start_index)
        && round_start_index == 1
        && !replay_input.start_frame_for_round(5, round_start_index)
        && RollbackNextReplayRoundIndex(0, 5, next_replay_round)
        && next_replay_round == 1
        && RollbackNextReplayRoundIndex(3, 5, next_replay_round)
        && next_replay_round == 4
        && !RollbackNextReplayRoundIndex(4, 5, next_replay_round)
        && next_replay_round == 0
        && replay_alignment_matrix_ok
        && RollbackPredictionLeadForDepth(6, 2, 12, prediction_lead)
        && prediction_lead == 8
        && !RollbackPredictionLeadForDepth(11, 2, 12, prediction_lead)
        && RollbackFixtureBarrierValid(
            fixture_local, fixture, fixture_local.pair_epoch)
        && RollbackFixtureBarriersMatch(
            fixture_local, fixture_peer, fixture,
            fixture_local.pair_epoch)
        && !RollbackFixtureBarriersMatch(
            fixture_local, fixture_wrong_frame, fixture,
            fixture_local.pair_epoch)
        && RollbackFixtureBarrierServiceDue(false, true, 120, 120, 12)
        && !RollbackFixtureBarrierServiceDue(false, true, 121, 120, 12)
        && RollbackFixtureBarrierServiceDue(false, false, 120, 120, 12)
        && RollbackFixtureBarrierServiceDue(false, false, 132, 120, 12)
        && !RollbackFixtureBarrierServiceDue(false, false, 133, 120, 12)
        && RollbackFixtureBarrierServiceDue(true, false, 133, 120, 12)
        && RollbackFixtureBarrierServiceDue(true, false, 10000, 120, 12)
        && !RollbackFixtureShouldBeginDatagramHold(fixture, 1, 120)
        && RollbackFixtureShouldBeginDatagramHold(fixture, 0, 120)
        && !RollbackFixtureShouldReleaseDatagrams(fixture, 125)
        && RollbackFixtureShouldReleaseDatagrams(fixture, 126)
        && !RollbackFixtureShouldReleaseDatagrams(fixture, 127)
        && RollbackFixtureInputForSlot(fixture, 0, 0, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 1, 0, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 1, 0x1234u)
            == kRollbackFixtureSlot0ApproachInput
        && RollbackFixtureInputForSlot(fixture, 0, 2, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 3, 0x1234u)
            == kRollbackFixtureSlot0ApproachInput
        && RollbackFixtureInputForSlot(fixture, 0, 69, 0x1234u)
            == kRollbackFixtureSlot0ApproachInput
        && RollbackFixtureInputForSlot(fixture, 1, 69, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 70, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 79, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 1, 79, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 90, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 119, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 1, 107, 0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(
            fixture, 1, RollbackFixturePreCorrectionActionStart(fixture),
            0x1234u)
            == kRollbackFixtureBasicActionInput
        && RollbackFixtureInputForSlot(
            fixture, 0, RollbackFixturePreCorrectionActionStart(fixture),
            0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(
            fixture, 1,
            RollbackFixturePreCorrectionActionStart(fixture)
                + kRollbackFixturePreCorrectionActionUpdates - 1,
            0x1234u)
            == kRollbackFixtureBasicActionInput
        && RollbackFixtureInputForSlot(
            fixture, 1,
            RollbackFixturePreCorrectionActionStart(fixture)
                + kRollbackFixturePreCorrectionActionUpdates,
            0x1234u)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 120, 0)
            == kRollbackFixtureBasicActionInput
        && RollbackFixtureInputForSlot(fixture, 1, 120, 0)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 1, 122, 0)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 1, 123, 0)
            == kRollbackFixtureNeutralInput
        && RollbackFixtureInputForSlot(fixture, 0, 126, 0x5678u)
            == 0x5678u
        ;

    const uint64_t epoch_a = ComputeRollbackPairEpochMaterial(
        1, 2, 3, 4, 12, 1, RollbackLifecycleMode::StockOnlinePvp,
        5, 6, 0x10003, 2, fixture.hash());
    const uint64_t epoch_b = ComputeRollbackPairEpochMaterial(
        1, 2, 4, 3, 12, 1, RollbackLifecycleMode::StockOnlinePvp,
        5, 6, 0x10003, 2, fixture.hash());
    const uint64_t other_fixture_epoch = ComputeRollbackPairEpochMaterial(
        1, 2, 3, 4, 12, 1, RollbackLifecycleMode::StockOnlinePvp,
        5, 6, 0x10003, 2, fixture.hash() + 1);

    const auto selection = RollbackStockOnlineLabDriver::desired(7, 6, 3);
    const bool selection_ok = selection.left_character == "007"
        && selection.right_character == "006"
        && selection.stage == "STG003"
        && selection.rounds_to_win == 3
        && RollbackStockOnlineLabDriver::accepts(
            selection, "007", "006", "STG003")
        && !RollbackStockOnlineLabDriver::accepts(
            selection, "006", "006", "STG003");
    const bool steam_connection_ok =
        RollbackStockOnlineLabDriver::steam_connection_complete(
            true, 1, 0x1000, true)
        && !RollbackStockOnlineLabDriver::steam_connection_complete(
            true, 1, 0x1000, false)
        && !RollbackStockOnlineLabDriver::steam_connection_complete(
            true, 3, 0x1000, true)
        && RollbackStockOnlineLabDriver::steam_connection_complete(
            false, 3, 0x1000, false)
        && !RollbackStockOnlineLabDriver::steam_connection_complete(
            false, 1, 0x1000, true)
        && !RollbackStockOnlineLabDriver::steam_connection_complete(
            true, 1, 0, true);
    using CleanupPhase = RollbackStockOnlineLabDriver::CleanupPhase;
    const bool cleanup_ok =
        RollbackStockOnlineLabDriver::cleanup_phase(
            true, false, false, false, false, false, false, false)
            == CleanupPhase::ExitBattle
        && RollbackStockOnlineLabDriver::cleanup_phase(
            true, false, false, false, true, false, false, false)
            == CleanupPhase::AwaitLobby
        && RollbackStockOnlineLabDriver::cleanup_phase(
            false, true, false, false, true, false, false, false)
            == CleanupPhase::DestroyRoom
        && RollbackStockOnlineLabDriver::cleanup_phase(
            false, true, false, false, true, true, false, false)
            == CleanupPhase::ExitPlayerMatch
        && RollbackStockOnlineLabDriver::cleanup_phase(
            false, false, true, false, true, true, false, false)
            == CleanupPhase::ResetOnlineStatus
        && RollbackStockOnlineLabDriver::cleanup_phase(
            false, false, false, true, true, true, true, false)
            == CleanupPhase::StabilizeMainMenu
        && RollbackStockOnlineLabDriver::cleanup_phase(
            false, false, false, true, true, true, true, true)
            == CleanupPhase::Complete;
    const bool cleanup_role_ok =
        RollbackStockOnlineLabDriver::destroys_room(true)
        && RollbackStockOnlineLabDriver::destroys_room(false)
        && std::wcscmp(
            RollbackStockOnlineLabDriver::battle_exit_menu(),
            L"BattleMenu") == 0
        && std::wcscmp(
            RollbackStockOnlineLabDriver::battle_exit_command(),
            L"LuxResultMenu::GoBackToLobby") == 0
        && std::wcscmp(
            RollbackStockOnlineLabDriver::battle_exit_confirm_menu(),
            L"Result") == 0
        && std::wcscmp(
            RollbackStockOnlineLabDriver::battle_exit_confirm_command(),
            L"OnDecide") == 0;
    const bool ownership_policy_ok =
        !RollbackStockOnlineLabDriver::ownership_ready(
            false, false, false, true)
        && RollbackStockOnlineLabDriver::ownership_ready(
            true, false, false, true)
        && !RollbackStockOnlineLabDriver::ownership_ready(
            true, false, false, false)
        && RollbackStockOnlineLabDriver::ownership_ready(
            true, true, true, false);
    using BattleRateDecision =
        RollbackStockOnlineLabDriver::BattleRateWindowDecision;
    RollbackStockOnlineLabDriver::BattleRateGate fail_then_pass {};
    fail_then_pass.begin_window();
    const auto first_fail = fail_then_pass.finish_window(57.999);
    fail_then_pass.begin_window();
    const auto second_pass = fail_then_pass.finish_window(58.0);
    RollbackStockOnlineLabDriver::BattleRateGate fail_twice {};
    fail_twice.begin_window();
    const auto first_of_two_failures = fail_twice.finish_window(57.999);
    fail_twice.begin_window();
    const auto second_failure = fail_twice.finish_window(57.999);
    RollbackStockOnlineLabDriver::BattleRateGate interrupted {};
    interrupted.begin_window();
    interrupted.interrupt_window();
    const bool battle_rate_window_ok =
        first_fail == BattleRateDecision::Retry
        && second_pass == BattleRateDecision::Passed
        && first_of_two_failures == BattleRateDecision::Retry
        && second_failure == BattleRateDecision::Failed
        && fail_then_pass.completed_windows == 2
        && fail_then_pass.complete && fail_then_pass.ok
        && fail_twice.completed_windows == 2
        && fail_twice.complete && !fail_twice.ok
        && interrupted.completed_windows == 0
        && !interrupted.window_active
        && !interrupted.complete && !interrupted.ok
        && RollbackStockOnlineLabDriver::battle_rate_window_decision(0, 58.0)
            == BattleRateDecision::Passed
        && RollbackStockOnlineLabDriver::battle_rate_window_decision(1, 60.0)
            == BattleRateDecision::Passed
        && RollbackStockOnlineLabDriver::battle_rate_sample_continuous(
            true, true, true, true, 120, 119)
        && !RollbackStockOnlineLabDriver::battle_rate_sample_continuous(
            true, true, true, true, 118, 119)
        && !RollbackStockOnlineLabDriver::battle_rate_sample_continuous(
            false, true, true, true, 120, 119)
        && !RollbackStockOnlineLabDriver::battle_rate_sample_continuous(
            true, true, false, true, 120, 119);
    RollbackStockOnlineLabDriver stock_driver {};
    stock_driver.reset(7, 6, 3);
    RollbackStockOnlineLabDriver replay_stock_driver {};
    replay_stock_driver.reset("00B", "004", 0x106);
    const bool replay_selection_ok =
        replay_stock_driver.selection().left_character == "00B"
        && replay_stock_driver.selection().right_character == "004"
        && replay_stock_driver.selection().stage == "STG006_R";
    const bool replay_seed_sequence_ok =
        // The authenticated lobby gate permits one request-bound seed arm.
        RollbackStockOnlineLabDriver::stock_lobby_ready_gate(
            true, true, true, true, true)
        && !RollbackStockOnlineLabDriver::stock_lobby_ready_gate(
            true, true, true, true, false)
        && RollbackStockOnlineLabDriver::replay_seed_start_decision(
            false, false, false, false)
            == RollbackStockOnlineLabDriver::
                ReplaySeedStartDecision::PassThrough
        && RollbackStockOnlineLabDriver::replay_seed_start_decision(
            true, false, true, false)
            == RollbackStockOnlineLabDriver::
                ReplaySeedStartDecision::UnexpectedLauncher
        && RollbackStockOnlineLabDriver::replay_seed_start_decision(
            true, true, false, false)
            == RollbackStockOnlineLabDriver::
                ReplaySeedStartDecision::UnexpectedFire
        && RollbackStockOnlineLabDriver::replay_seed_start_decision(
            true, true, true, false)
            == RollbackStockOnlineLabDriver::ReplaySeedStartDecision::Apply
        && RollbackStockOnlineLabDriver::replay_seed_start_decision(
            false, true, true, true)
            == RollbackStockOnlineLabDriver::
                ReplaySeedStartDecision::DuplicateStart
        && RollbackStockOnlineLabDriver::replay_seed_allows_battle_release(
            false, false)
        && RollbackStockOnlineLabDriver::replay_seed_allows_battle_release(
            true, true)
        && !RollbackStockOnlineLabDriver::replay_seed_allows_battle_release(
            true, false);
    RollbackStockOnlineLabDriver::ReplaySeedOneShotModel seed_apply {};
    const bool seed_apply_lifecycle_ok = seed_apply.arm()
        && seed_apply.start(true, true, true)
            == RollbackStockOnlineLabDriver::ReplaySeedStartDecision::Apply
        && !seed_apply.token_live
        && seed_apply.consumed_arm_live
        && !seed_apply.sticky_fault
        && !seed_apply.setter_failed
        && !seed_apply.arm()
        && seed_apply.start(true, true, true)
            == RollbackStockOnlineLabDriver::
                ReplaySeedStartDecision::DuplicateStart
        && seed_apply.sticky_fault;
    seed_apply.clear();
    const bool seed_clear_and_rearm_ok = !seed_apply.token_live
        && !seed_apply.consumed_arm_live
        && !seed_apply.sticky_fault
        && !seed_apply.setter_failed
        && seed_apply.arm();
    RollbackStockOnlineLabDriver::ReplaySeedOneShotModel seed_wrong_launcher {};
    const bool seed_wrong_launcher_sticky_ok = seed_wrong_launcher.arm()
        && seed_wrong_launcher.start(false, true, true)
            == RollbackStockOnlineLabDriver::
                ReplaySeedStartDecision::UnexpectedLauncher
        && seed_wrong_launcher.token_live
        && !seed_wrong_launcher.consumed_arm_live
        && seed_wrong_launcher.sticky_fault
        && seed_wrong_launcher.start(true, true, true)
            == RollbackStockOnlineLabDriver::ReplaySeedStartDecision::Apply
        && seed_wrong_launcher.consumed_arm_live
        && seed_wrong_launcher.sticky_fault;
    RollbackStockOnlineLabDriver::ReplaySeedOneShotModel seed_wrong_fire {};
    const bool seed_wrong_fire_consumes_ok = seed_wrong_fire.arm()
        && seed_wrong_fire.start(true, false, true)
            == RollbackStockOnlineLabDriver::
                ReplaySeedStartDecision::UnexpectedFire
        && !seed_wrong_fire.token_live
        && seed_wrong_fire.consumed_arm_live
        && seed_wrong_fire.sticky_fault
        && !seed_wrong_fire.arm();
    RollbackStockOnlineLabDriver::ReplaySeedOneShotModel seed_setter_failure {};
    const bool seed_setter_failure_sticky_ok = seed_setter_failure.arm()
        && seed_setter_failure.start(true, true, false)
            == RollbackStockOnlineLabDriver::ReplaySeedStartDecision::Apply
        && seed_setter_failure.consumed_arm_live
        && seed_setter_failure.setter_failed
        && seed_setter_failure.sticky_fault
        && !seed_setter_failure.arm();
    RollbackStockOnlineLabDriver::SetupObservation setup {};
    setup.in_main_menu = true;
    const bool setup_main = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::EnterPlayerMatch;
    setup = {};
    setup.in_lobby = true;
    const bool setup_create = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::CreateRoom;
    setup.room_created = true;
    const bool setup_invite = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::InviteGuest;
    setup.guest_joined = true;
    const bool setup_negotiate = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::NegotiateRollback;
    setup.rollback_contract_ready = true;
    const bool setup_ready = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::Ready;
    setup.in_lobby = false;
    setup.in_setup = true;
    const bool setup_character = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::CharacterSelect;
    setup.characters_synchronized = true;
    const bool setup_stage = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::StageSelect;
    setup.stage_synchronized = true;
    setup.selection_bilateral = true;
    const bool setup_battle = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::EnterBattle;
    setup.native_battle_running = true;
    const bool setup_active = stock_driver.observe_setup(setup)
        == RollbackStockOnlineLabDriver::SetupPhase::Active;
    RollbackStockOnlineLabDriver::CleanupObservation cleanup {};
    cleanup.scene_identity_valid = true;
    cleanup.in_battle = true;
    const bool cleanup_exit = stock_driver.observe_cleanup(cleanup)
        == CleanupPhase::ExitBattle;
    cleanup.in_battle = false;
    cleanup.in_lobby = true;
    cleanup.scene_identity_valid = false;
    (void)stock_driver.observe_cleanup(cleanup);
    const bool cleanup_unknown_reset =
        stock_driver.cleanup_out_of_battle_observations() == 0;
    cleanup.scene_identity_valid = true;
    (void)stock_driver.observe_cleanup(cleanup);
    const bool cleanup_one =
        stock_driver.cleanup_out_of_battle_observations() == 1
        && !stock_driver.cleanup_out_of_battle_stable();
    (void)stock_driver.observe_cleanup(cleanup);
    const bool cleanup_two =
        stock_driver.cleanup_out_of_battle_observations() == 2
        && !stock_driver.cleanup_out_of_battle_stable();
    cleanup.scene_identity_valid = false;
    (void)stock_driver.observe_cleanup(cleanup);
    const bool cleanup_second_unknown_reset =
        stock_driver.cleanup_out_of_battle_observations() == 0;
    cleanup.scene_identity_valid = true;
    (void)stock_driver.observe_cleanup(cleanup);
    (void)stock_driver.observe_cleanup(cleanup);
    cleanup.in_battle = true;
    cleanup.in_lobby = false;
    (void)stock_driver.observe_cleanup(cleanup);
    const bool cleanup_battle_reset =
        stock_driver.cleanup_out_of_battle_observations() == 0;
    cleanup.in_battle = false;
    cleanup.in_lobby = true;
    (void)stock_driver.observe_cleanup(cleanup);
    (void)stock_driver.observe_cleanup(cleanup);
    (void)stock_driver.observe_cleanup(cleanup);
    const bool cleanup_observed =
        stock_driver.cleanup_out_of_battle_stable()
        && stock_driver.cleanup_out_of_battle_observations() == 3;
    const bool stateful_driver_ok = replay_seed_sequence_ok
        && seed_apply_lifecycle_ok && seed_clear_and_rearm_ok
        && seed_wrong_launcher_sticky_ok && seed_wrong_fire_consumes_ok
        && seed_setter_failure_sticky_ok
        && setup_main && setup_create
        && setup_invite && setup_negotiate && setup_ready
        && setup_character && setup_stage
        && setup_battle && setup_active && cleanup_exit
        && cleanup_unknown_reset && cleanup_one && cleanup_two
        && cleanup_second_unknown_reset && cleanup_battle_reset
        && cleanup_observed
        && RollbackStockOnlineLabDriver::accepts(
            stock_driver.selection(), "007", "006", "STG003");

    const bool baseline_verification_gate_ok =
        !RollbackLaunchBaselineVerificationRequired(
            false, RollbackLaunchBarrierStage::BattleBaseline)
        && !RollbackLaunchBaselineVerificationRequired(
            true, RollbackLaunchBarrierStage::None)
        && RollbackLaunchBaselineVerificationRequired(
            true, RollbackLaunchBarrierStage::BattleBaseline)
        && RollbackLaunchBaselineVerificationRequired(
            true, RollbackLaunchBarrierStage::BattleBaselineAccepted);

    const bool ok = RollbackLaunchBarrierValid(local)
        && baseline_verification_gate_ok
        && connect_fail_observation_ok
        && ownership_policy_ok
        && battle_rate_window_ok
        && replay_selection_ok
        && steam_identity.valid()
        && changed_lobby.valid() && !(changed_lobby == steam_identity)
        && RollbackSteamSessionIdentityConflicts(
            steam_identity, changed_lobby)
        && RollbackSteamSessionIdentityConflictMask(
            steam_identity, changed_lobby)
            == RollbackSteamIdentityConflictLobby
        && !RollbackSteamSessionIdentityConflicts(
            steam_identity, transient_session)
        && RollbackSteamSessionIdentityConflictMask(
            steam_identity, transient_session)
            == RollbackSteamIdentityConflictNone
        && RollbackSteamSessionIdentityConflicts(
            steam_identity, changed_epoch)
        && RollbackSteamSessionIdentityConflictMask(
            steam_identity, changed_epoch)
            == RollbackSteamIdentityConflictSessionConnection
        && RollbackSteamStablePeerConflictMask(
            steam_identity, changed_lobby)
            == RollbackSteamIdentityConflictLobby
        && RollbackSteamNativeEpochConflictMask(
            steam_identity, changed_lobby)
            == RollbackSteamIdentityConflictNone
        && RollbackSteamStablePeerConflictMask(
            steam_identity, changed_epoch)
            == RollbackSteamIdentityConflictNone
        && RollbackSteamNativeEpochConflictMask(
            steam_identity, changed_epoch)
            == RollbackSteamIdentityConflictSessionConnection
        && RollbackSteamObservationBelongsToEpoch(
            steam_identity, terminal_after_pointer_clear)
        && DecideRollbackSteamObservation(
            {}, incomplete_initial)
            == RollbackSteamObservationDecision::WaitForInitialReady
        && DecideRollbackSteamObservation({}, steam_identity)
            == RollbackSteamObservationDecision::AcceptInitialEpoch
        && DecideRollbackSteamObservation(
            {}, terminal_after_pointer_clear)
            == RollbackSteamObservationDecision::RejectInitialTerminal
        && DecideRollbackSteamObservation(
            steam_identity, transient_session)
            == RollbackSteamObservationDecision::PreserveTransientGap
        && DecideRollbackSteamObservation(
            steam_identity, steam_identity)
            == RollbackSteamObservationDecision::RefreshSameEpoch
        && DecideRollbackSteamObservation(
            steam_identity, changed_lobby)
            == RollbackSteamObservationDecision
                ::RejectStableIdentityChange
        && DecideRollbackSteamObservation(
            steam_identity, changed_epoch)
            == RollbackSteamObservationDecision::RejectNativeEpochChange
        && DecideRollbackSteamObservation(
            steam_identity, reused_pointers_new_lifecycle)
            == RollbackSteamObservationDecision::RejectNativeEpochChange
        && DecideRollbackSteamObservation(
            steam_identity, terminal_after_pointer_clear)
            == RollbackSteamObservationDecision::RevokeTerminalEpoch
        && steam_identity.native_epoch_key() != 0
        && steam_identity.native_epoch_key()
            != changed_epoch.native_epoch_key()
        && RollbackSessionContractValid(contract_host)
        && RollbackSessionContractValid(contract_guest)
        && RollbackSessionContractsMatch(contract_host, contract_guest)
        && !RollbackSessionContractsMatch(contract_host, stale_contract)
        && !RollbackSessionContractsMatch(contract_host, mismatched_contract)
        && !RollbackSessionContractsMatch(contract_host, duplicate_slot)
        && RollbackLaunchBarrierValid(accepted)
        && RollbackLaunchBarriersMatch(local, peer)
        && RollbackLaunchBarriersMatch(local, accepted)
        && RollbackLaunchBarriersMatch(local, stock_input_skew)
        && RollbackLaunchBarriersMatch(local, native_frame_skew)
        && RollbackLaunchBarrierIsStaleDuplicate(accepted, peer)
        && !RollbackLaunchBarriersMatch(local, wrong_slot)
        && !RollbackLaunchBarriersMatch(local, wrong_hash)
        && !RollbackLaunchBarriersMatch(local, wrong_stage)
        && !RollbackLaunchBarrierValid(missing_stage)
        && launch_inbox_ok && accepted_peer_first_ok
        && epoch_a != 0 && epoch_a == epoch_b
        && other_fixture_epoch != epoch_a
        && fixture_ok && fast_hash_ok && transition_barrier_ok
        && asymmetric_terminal_proposal_ok
        && terminal_accepted_republish_ok
        && pre_round_barrier_ok
        && selection_ok && steam_connection_ok
        && cleanup_ok && cleanup_role_ok
        && stateful_driver_ok;
    std::printf(
        "rollback launch-contract self-test %s epoch=0x%llX\n",
        ok ? "passed" : "failed",
        static_cast<unsigned long long>(epoch_a));
    return ok ? 0 : 1;
}
