// ============================================================================
// Horse::RollbackLaunchContract
//
// Authenticated stock-online policy shared by the UDP worker and lifecycle
// gate.
// ============================================================================

#pragma once

#include "RollbackRuntimePolicy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse
{
    static constexpr uint32_t kRollbackDeterministicFixtureId = 1;
    static constexpr uint32_t kRollbackFixtureNeutralInput = 0x0000u;
    // LuxBattle_TickCharaInput consumes the canonical compact engine word:
    // buttons occupy bits 0..3 and horizontal direction components occupy
    // bits 10 and 11. Native slot 0 spawns on the right in this stock flow, so
    // approaching the opponent uses the left component (bit 10). 0x0040 is a
    // separate MoveVM notation-mask encoding and is ignored at this boundary.
    static constexpr uint32_t kRollbackFixtureSlot0ApproachInput = 0x0400u;
    // Canonical compact A+B break-attack chord. Ordinary A and B both produced
    // hit/audio/VFX for 007/006 without arming the verified HgSoulCamera
    // vibration path; the authored break attack covers the stronger hit path.
    static constexpr uint32_t kRollbackFixtureBasicActionInput = 0x0003u;
    static constexpr uint32_t kRollbackFixtureApproachUpdates = 70u;
    static constexpr uint32_t kRollbackFixtureSettleUpdates = 20u;
    // Let the non-delayed peer fill Gekko's prediction window before slot 0
    // begins the six-update send hold. This makes the correction replay the
    // complete contact presentation instead of depending on client startup
    // skew.
    static constexpr uint32_t kRollbackFixturePredictionLeadUpdates = 12u;
    static constexpr uint32_t
        kRollbackFixturePreCorrectionActionLeadUpdates = 33u;
    static constexpr uint32_t kRollbackFixturePreCorrectionActionStart =
        120u - kRollbackFixturePreCorrectionActionLeadUpdates;
    static constexpr uint32_t kRollbackFixturePreCorrectionActionUpdates = 3u;
    // The authored contact can dispatch presentation work after the six held
    // updates. Keep that proof interval independent from snapshot capacity:
    // history size is not a confirmation guarantee.
    static constexpr uint32_t kRollbackFixturePresentationTailUpdates = 20u;
    static constexpr uint32_t kRollbackFixtureCheckpointSettleUpdates = 10u;

    struct RollbackDeterministicInputConfig
    {
        uint32_t fixture_id {kRollbackDeterministicFixtureId};
        uint32_t correction_start {120};
        uint16_t hold_updates {6};
        uint16_t prediction_lead_updates {
            static_cast<uint16_t>(kRollbackFixturePredictionLeadUpdates)};
        uint8_t delay_owner_slot {0};
        bool enabled {false};

        constexpr bool valid() const noexcept
        {
            return !enabled
                || (fixture_id == kRollbackDeterministicFixtureId
                    && delay_owner_slot == 0
                    && correction_start > kRollbackFixtureApproachUpdates
                        + kRollbackFixtureSettleUpdates
                    && correction_start <= UINT32_MAX
                        - kRollbackFixturePresentationTailUpdates
                        - kRollbackFixtureCheckpointSettleUpdates
                    && hold_updates != 0
                    && correction_start <= UINT32_MAX
                        - (static_cast<uint32_t>(hold_updates) - 1u)
                    && hold_updates <= 32
                    && prediction_lead_updates != 0
                    && prediction_lead_updates <= 60);
        }

        constexpr uint64_t hash() const noexcept
        {
            uint64_t value = 1469598103934665603ull;
            const auto add = [&value](uint64_t scalar) constexpr {
                for (uint32_t i = 0; i < 8; ++i)
                {
                    value ^= static_cast<uint8_t>(scalar >> (i * 8));
                    value *= 1099511628211ull;
                }
            };
            add(fixture_id);
            add(correction_start);
            add(hold_updates);
            add(prediction_lead_updates);
            add(delay_owner_slot);
            add(enabled ? 1u : 0u);
            return value ? value : 1;
        }
    };

    static constexpr uint32_t RollbackFixtureLastHeldFrame(
        const RollbackDeterministicInputConfig& config) noexcept
    {
        return config.correction_start
            + (config.hold_updates == 0
                ? 0u : static_cast<uint32_t>(config.hold_updates) - 1u);
    }

    static constexpr uint32_t RollbackFixtureEvidenceStartFrame(
        const RollbackDeterministicInputConfig& config) noexcept
    {
        return config.correction_start;
    }

    static constexpr uint32_t RollbackFixtureEvidenceEndFrame(
        const RollbackDeterministicInputConfig& config) noexcept
    {
        return config.correction_start
            + kRollbackFixturePresentationTailUpdates;
    }

    static constexpr uint32_t RollbackFixtureCheckpointFrame(
        const RollbackDeterministicInputConfig& config) noexcept
    {
        return RollbackFixtureEvidenceEndFrame(config)
            + kRollbackFixtureCheckpointSettleUpdates;
    }

    static constexpr bool RollbackFixtureEvidenceFrame(
        const RollbackDeterministicInputConfig& config,
        uint32_t frame) noexcept
    {
        return config.enabled
            && frame >= RollbackFixtureEvidenceStartFrame(config)
            && frame <= RollbackFixtureEvidenceEndFrame(config);
    }

    static constexpr bool RollbackFixtureLoadAffectsEvidence(
        const RollbackDeterministicInputConfig& config,
        uint16_t input_delay,
        uint32_t load_frame) noexcept
    {
        const uint32_t earliest = config.correction_start > input_delay
            ? config.correction_start - input_delay : 0u;
        return config.enabled && load_frame >= earliest
            && load_frame <= config.correction_start;
    }

    enum class RollbackReplayInputAlignment : uint8_t
    {
        ExactConsumedFrame = 0,
    };

    static constexpr bool RollbackConsumedReplayIndex(
        uint32_t start_frame,
        uint32_t round_frame_count,
        uint32_t logical_frame,
        uint16_t input_delay,
        uint32_t& index) noexcept
    {
        // Delay changes when an input is submitted, never which replay input
        // belongs to a simulated frame.
        (void)input_delay;
        const uint64_t candidate = static_cast<uint64_t>(start_frame)
            + logical_frame;
        if (candidate >= round_frame_count) return false;
        index = static_cast<uint32_t>(candidate);
        return true;
    }

    // Replay rounds are bounded authored scripts, but the native round can
    // require additional simulation frames before publishing its terminal
    // state. Those frames have an explicit neutral-input meaning; they are
    // not missing authority. Keep the authored/tail distinction observable.
    static constexpr bool RollbackConsumedReplayIndexOrNeutralTail(
        uint32_t start_frame,
        uint32_t round_frame_count,
        uint32_t logical_frame,
        uint16_t input_delay,
        uint32_t& index,
        bool& authored) noexcept
    {
        (void)input_delay;
        index = 0;
        authored = false;
        if (round_frame_count == 0) return false;
        const uint64_t candidate = static_cast<uint64_t>(start_frame)
            + logical_frame;
        if (candidate > UINT32_MAX) return false;
        index = static_cast<uint32_t>(candidate);
        authored = candidate < round_frame_count;
        return true;
    }

    static constexpr bool RollbackSubmittedReplayIndex(
        uint32_t start_frame,
        uint32_t round_frame_count,
        uint32_t submission_frame,
        uint16_t input_delay,
        uint32_t& index) noexcept
    {
        const uint64_t consumed_frame =
            static_cast<uint64_t>(submission_frame) + input_delay;
        if (consumed_frame > UINT32_MAX) return false;
        return RollbackConsumedReplayIndex(
            start_frame, round_frame_count,
            static_cast<uint32_t>(consumed_frame), input_delay, index);
    }

    // A replay is an immutable pair of compact engine-input streams. The
    // path is deliberately excluded from the authenticated material: both
    // peers authenticate the file digest and the selected round instead.
    struct RollbackReplayInputConfig
    {
        static constexpr size_t kMaximumRounds = 64;

        bool enabled {false};
        bool swap_players {false};
        RollbackReplayInputAlignment alignment {
            RollbackReplayInputAlignment::ExactConsumedFrame};
        uint8_t reserved {0};
        uint32_t round_index {0};
        uint32_t start_frame {0};
        uint32_t round_frame_count {0};
        uint32_t replay_random_seed {0};
        uint8_t round_start_count {0};
        std::array<uint8_t, 3> round_start_reserved {};
        std::array<uint16_t, kMaximumRounds> round_start_frames {};
        std::array<uint64_t, 2> round_input_hash {};
        std::array<uint8_t, 32> file_sha256 {};

        constexpr bool requested_valid() const noexcept
        {
            bool digest_nonzero = false;
            for (uint8_t value : file_sha256)
                digest_nonzero = digest_nonzero || value != 0;
            return !enabled
                || (digest_nonzero
                    && reserved == 0
                    && round_index < 64
                    && replay_random_seed != 0
                    && round_start_count != 0
                    && round_start_count <= kMaximumRounds
                    && round_index < round_start_count
                    && round_start_reserved[0] == 0
                    && round_start_reserved[1] == 0
                    && round_start_reserved[2] == 0
                    && start_frame == round_start_frames[round_index]
                    && alignment
                        == RollbackReplayInputAlignment::ExactConsumedFrame);
        }

        constexpr bool resolved_valid() const noexcept
        {
            return requested_valid()
                && (!enabled
                    || (round_frame_count != 0
                        && start_frame < round_frame_count
                        && round_input_hash[0] != 0
                        && round_input_hash[1] != 0));
        }

        constexpr uint8_t replay_player_for_native_slot(
            uint8_t native_slot) const noexcept
        {
            return static_cast<uint8_t>(
                swap_players ? 1u - native_slot : native_slot);
        }

        constexpr bool start_frame_for_round(
            uint32_t replay_round, uint32_t& value) const noexcept
        {
            value = 0;
            if (!enabled || replay_round >= round_start_count)
                return false;
            value = round_start_frames[replay_round];
            return true;
        }

        constexpr bool input_index(
            uint32_t submission_frame,
            uint16_t input_delay,
            uint32_t& index) const noexcept
        {
            return enabled && alignment
                    == RollbackReplayInputAlignment::ExactConsumedFrame
                && RollbackSubmittedReplayIndex(
                start_frame, round_frame_count, submission_frame,
                input_delay, index);
        }

        constexpr bool consumed_index(
            uint32_t logical_frame,
            uint16_t input_delay,
            uint32_t& index) const noexcept
        {
            return enabled && alignment
                    == RollbackReplayInputAlignment::ExactConsumedFrame
                && RollbackConsumedReplayIndex(
                    start_frame, round_frame_count, logical_frame,
                    input_delay, index);
        }

        constexpr bool consumed_index_or_neutral_tail(
            uint32_t logical_frame,
            uint16_t input_delay,
            uint32_t& index,
            bool& authored) const noexcept
        {
            return enabled && alignment
                    == RollbackReplayInputAlignment::ExactConsumedFrame
                && RollbackConsumedReplayIndexOrNeutralTail(
                    start_frame, round_frame_count, logical_frame,
                    input_delay, index, authored);
        }

        constexpr uint64_t hash() const noexcept
        {
            uint64_t value = 1469598103934665603ull;
            const auto add = [&value](uint64_t scalar) constexpr {
                for (uint32_t i = 0; i < 8; ++i)
                {
                    value ^= static_cast<uint8_t>(scalar >> (i * 8));
                    value *= 1099511628211ull;
                }
            };
            add(enabled ? 1u : 0u);
            add(swap_players ? 1u : 0u);
            add(static_cast<uint8_t>(alignment));
            add(round_index);
            add(start_frame);
            add(round_frame_count);
            add(replay_random_seed);
            add(round_start_count);
            for (uint32_t round = 0; round < round_start_count; ++round)
                add(round_start_frames[round]);
            add(round_input_hash[0]);
            add(round_input_hash[1]);
            for (uint8_t byte : file_sha256) add(byte);
            return value ? value : 1;
        }
    };

    static constexpr bool RollbackNextReplayRoundIndex(
        uint32_t current_round,
        uint32_t round_count,
        uint32_t& next_round) noexcept
    {
        if (round_count == 0 || current_round >= round_count - 1u)
        {
            next_round = 0;
            return false;
        }
        next_round = current_round + 1u;
        return true;
    }

    static constexpr bool RollbackPredictionLeadForDepth(
        uint16_t rollback_depth,
        uint16_t input_delay,
        uint16_t rollback_window,
        uint16_t& prediction_lead) noexcept
    {
        const uint32_t lead = static_cast<uint32_t>(rollback_depth)
            + input_delay;
        if (rollback_depth == 0 || input_delay == 0
            || lead > rollback_window || lead > 60)
        {
            prediction_lead = 0;
            return false;
        }
        prediction_lead = static_cast<uint16_t>(lead);
        return true;
    }

#pragma pack(push, 1)
    struct RollbackFixtureBarrierMessage
    {
        uint8_t version {1};
        uint8_t local_player_slot {0};
        uint16_t reserved {0};
        uint32_t fixture_id {0};
        uint32_t submission_frame {0};
        uint64_t pair_epoch {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackFixtureBarrierMessage) == 20);

    static constexpr bool RollbackFixtureBarrierValid(
        const RollbackFixtureBarrierMessage& message,
        const RollbackDeterministicInputConfig& config,
        uint64_t pair_epoch) noexcept
    {
        return message.version == 1
            && message.local_player_slot < 2
            && message.reserved == 0
            && message.fixture_id == config.fixture_id
            && message.submission_frame == config.correction_start
            && message.pair_epoch != 0
            && message.pair_epoch == pair_epoch;
    }

    static constexpr bool RollbackFixtureBarriersMatch(
        const RollbackFixtureBarrierMessage& local,
        const RollbackFixtureBarrierMessage& remote,
        const RollbackDeterministicInputConfig& config,
        uint64_t pair_epoch) noexcept
    {
        return RollbackFixtureBarrierValid(local, config, pair_epoch)
            && RollbackFixtureBarrierValid(remote, config, pair_epoch)
            && local.local_player_slot != remote.local_player_slot
            && local.fixture_id == remote.fixture_id
            && local.submission_frame == remote.submission_frame
            && local.pair_epoch == remote.pair_epoch;
    }

    // Before the local record exists, only enter the rendezvous at the
    // configured correction window. Once published, keep servicing and
    // retransmitting it until the fixture transaction completes. Otherwise
    // the non-delayed peer can run beyond the window after receiving the
    // delayed peer's record while its own reciprocal record was dropped.
    static constexpr bool RollbackFixtureBarrierServiceDue(
        bool local_record_valid,
        bool delay_owner,
        uint32_t submission_frame,
        uint32_t correction_frame,
        uint16_t prediction_lead_updates) noexcept
    {
        if (local_record_valid) return true;
        if (delay_owner) return submission_frame == correction_frame;
        return submission_frame >= correction_frame
            && submission_frame <= correction_frame
                + prediction_lead_updates;
    }

    static constexpr bool RollbackFixtureUpdateActive(
        const RollbackDeterministicInputConfig& config,
        uint64_t update) noexcept
    {
        return config.enabled
            && update >= config.correction_start
            && update < static_cast<uint64_t>(config.correction_start)
                + config.hold_updates;
    }

    static constexpr bool RollbackFixtureShouldBeginDatagramHold(
        const RollbackDeterministicInputConfig& config,
        uint8_t local_slot,
        uint64_t update) noexcept
    {
        return config.enabled
            && local_slot == config.delay_owner_slot
            && update == config.correction_start;
    }

    static constexpr bool RollbackFixtureShouldReleaseDatagrams(
        const RollbackDeterministicInputConfig& config,
        uint64_t completed_updates) noexcept
    {
        return config.enabled
            && completed_updates ==
                static_cast<uint64_t>(config.correction_start)
                    + config.hold_updates;
    }

    static constexpr bool RollbackFixtureApproachActive(
        const RollbackDeterministicInputConfig& config,
        uint64_t update) noexcept
    {
        // Approach immediately after ownership, then settle. Anchoring this
        // window immediately before correction made 007's run momentum reach
        // the opponent only after the speculative action had already fired.
        return config.enabled
            && update < kRollbackFixtureApproachUpdates;
    }

    static constexpr bool RollbackFixtureSettleActive(
        const RollbackDeterministicInputConfig& config,
        uint64_t update) noexcept
    {
        return config.enabled
            && update >= kRollbackFixtureApproachUpdates
            && update < kRollbackFixtureApproachUpdates
                + kRollbackFixtureSettleUpdates;
    }

    static constexpr uint32_t RollbackFixturePreCorrectionActionStart(
        const RollbackDeterministicInputConfig& config) noexcept
    {
        return config.correction_start
            - kRollbackFixturePreCorrectionActionLeadUpdates;
    }

    static constexpr uint32_t RollbackFixtureInputForSlot(
        const RollbackDeterministicInputConfig& config,
        uint8_t slot,
        uint64_t update,
        uint32_t native_input) noexcept
    {
        if (RollbackFixtureUpdateActive(config, update))
        {
            if (slot == config.delay_owner_slot)
                return kRollbackFixtureBasicActionInput;
            return kRollbackFixtureNeutralInput;
        }
        if (update < config.correction_start)
        {
            // Start the same real move on both peers late enough that its hit
            // presentation lands strictly after the first six-update Load
            // boundary. The correction then must discard and replay the
            // native audio/VFX/camera/transition buckets.
            const uint32_t action_start =
                RollbackFixturePreCorrectionActionStart(config);
            if (update >= action_start
                && update < action_start
                    + kRollbackFixturePreCorrectionActionUpdates)
            {
                // Keep the delayed slot neutral here. Its correction action
                // must differ from every possible repeated-last-input
                // prediction; the non-delayed fighter creates the contact
                // presentation that the correction must discard and replay.
                return slot == config.delay_owner_slot
                    ? kRollbackFixtureNeutralInput
                    : kRollbackFixtureBasicActionInput;
            }
            if (slot != config.delay_owner_slot)
                return kRollbackFixtureNeutralInput;
            if (!RollbackFixtureApproachActive(config, update))
                return kRollbackFixtureNeutralInput;

            // Arm the run after ownership is fully established. The former
            // forward/neutral/forward sequence began on update zero, whose
            // first edge can coincide with the ownership handoff and be lost.
            // Both forward edges now occur on ordinary owned updates.
            return update == 0u || update == 2u
                ? kRollbackFixtureNeutralInput
                : kRollbackFixtureSlot0ApproachInput;
        }
        return native_input;
    }

    enum class RollbackLifecycleMode : uint8_t
    {
        StockOnlinePvp = 0,
    };

    static constexpr bool RollbackLifecycleModeValid(
        RollbackLifecycleMode mode) noexcept
    {
        return mode == RollbackLifecycleMode::StockOnlinePvp;
    }

    static constexpr const char* RollbackLifecycleModeName(
        RollbackLifecycleMode mode) noexcept
    {
        (void)mode;
        return "stock-online-pvp";
    }

    static constexpr uint64_t ComputeRollbackPairEpochMaterial(
        uint64_t build_id,
        uint64_t schema_id,
        uint8_t local_peer,
        uint8_t remote_peer,
        uint16_t rollback_window,
        uint16_t input_delay,
        RollbackLifecycleMode lifecycle_mode,
        uint64_t round_start_digest,
        uint64_t stage_layout_digest,
        uint32_t native_stage_identity,
        uint32_t round_ordinal,
        uint64_t deterministic_fixture_hash = 0) noexcept
    {
        uint64_t value = 1469598103934665603ull;
        const auto add = [&value](uint64_t scalar) constexpr {
            for (uint32_t i = 0; i < 8; ++i)
            {
                value ^= static_cast<uint8_t>(scalar >> (i * 8));
                value *= 1099511628211ull;
            }
        };
        const uint8_t peer_low = local_peer < remote_peer
            ? local_peer : remote_peer;
        const uint8_t peer_high = local_peer < remote_peer
            ? remote_peer : local_peer;
        add(build_id);
        add(schema_id);
        add(peer_low);
        add(peer_high);
        add(rollback_window);
        add(input_delay);
        add(static_cast<uint8_t>(lifecycle_mode));
        add(round_start_digest);
        add(stage_layout_digest);
        add(native_stage_identity);
        add(round_ordinal);
        add(deterministic_fixture_hash);
        return value ? value : 1;
    }

    enum class RollbackSteamNativeTerminalEvidence : uint8_t
    {
        None = 0,
        LuxorRetryExhausted = 1,
        JoinSceneFailure = 2,
        P2PSessionConnectFail = 3,
    };

    struct RollbackSteamP2PConnectFailObservation
    {
        uint64_t sequence {0};
        uint64_t lifecycle_serial {0};
        uint64_t remote_steam_id {0};
        uint8_t error {0xff};
        bool stream_overflow {false};
    };

    enum class RollbackSteamP2PConnectFailDisposition : uint8_t
    {
        Invalid = 0,
        StaleLifecycle = 1,
        OtherRemote = 2,
        Terminal = 3,
        TerminalStreamOverflow = 4,
    };

    constexpr RollbackSteamP2PConnectFailDisposition
    ClassifyRollbackSteamP2PConnectFailObservation(
        const RollbackSteamP2PConnectFailObservation& observation,
        uint64_t current_lifecycle_serial,
        uint64_t current_remote_steam_id) noexcept
    {
        if (current_lifecycle_serial == 0 || current_remote_steam_id == 0)
            return RollbackSteamP2PConnectFailDisposition::Invalid;
        if (observation.stream_overflow)
            return RollbackSteamP2PConnectFailDisposition::
                TerminalStreamOverflow;
        if (observation.sequence == 0
            || observation.lifecycle_serial == 0
            || observation.remote_steam_id == 0)
            return RollbackSteamP2PConnectFailDisposition::Invalid;
        if (observation.lifecycle_serial != current_lifecycle_serial)
            return RollbackSteamP2PConnectFailDisposition::StaleLifecycle;
        if (observation.remote_steam_id != current_remote_steam_id)
            return RollbackSteamP2PConnectFailDisposition::OtherRemote;
        return RollbackSteamP2PConnectFailDisposition::Terminal;
    }

    struct RollbackSteamSessionIdentity
    {
        uint64_t lobby_id {0};
        uint64_t owner_steam_id {0};
        uint64_t local_steam_id {0};
        uint64_t remote_steam_id {0};
        uint64_t selection_hash {0};
        // Local native-session generation. These values are never sent to a
        // peer or included in the canonical snapshot schema: they bind the
        // Horse transport to the exact SC6 objects that proved readiness.
        uint64_t lifecycle_serial {0};
        uintptr_t named_session {0};
        uintptr_t session_info {0};
        uintptr_t connect_manager {0};
        uintptr_t active_connect {0};
        uintptr_t active_transport {0};
        uintptr_t session_connection {0};
        uint8_t active_connect_state {0xFF};
        uint8_t active_connect_sub_state {0xFF};
        bool named_session_valid {false};
        bool transport_connected {false};
        bool native_terminal {false};
        RollbackSteamNativeTerminalEvidence terminal_evidence {
            RollbackSteamNativeTerminalEvidence::None};

        constexpr bool stable_peer_valid() const noexcept
        {
            return lobby_id != 0 && owner_steam_id != 0
                && local_steam_id != 0 && remote_steam_id != 0
                && local_steam_id != remote_steam_id
                && (owner_steam_id == local_steam_id
                    || owner_steam_id == remote_steam_id)
                && selection_hash != 0;
        }

        constexpr bool native_epoch_complete() const noexcept
        {
            return lifecycle_serial != 0 && named_session != 0
                && session_info != 0 && connect_manager != 0
                && active_connect != 0 && active_transport != 0
                && session_connection != 0;
        }

        constexpr bool native_epoch_ready() const noexcept
        {
            return native_epoch_complete() && !native_terminal
                && terminal_evidence
                    == RollbackSteamNativeTerminalEvidence::None
                && named_session_valid && transport_connected;
        }

        constexpr bool valid() const noexcept
        {
            return stable_peer_valid() && native_epoch_ready();
        }

        constexpr bool same_native_epoch(
            const RollbackSteamSessionIdentity& other) const noexcept
        {
            return lifecycle_serial == other.lifecycle_serial
                && named_session == other.named_session
                && session_info == other.session_info
                && connect_manager == other.connect_manager
                && active_connect == other.active_connect
                && active_transport == other.active_transport
                && session_connection == other.session_connection;
        }

        constexpr uint64_t native_epoch_key() const noexcept
        {
            uint64_t value = 1469598103934665603ull;
            const auto add = [&value](uint64_t item) constexpr {
                value ^= item;
                value *= 1099511628211ull;
            };
            add(lifecycle_serial);
            add(static_cast<uint64_t>(named_session));
            add(static_cast<uint64_t>(session_info));
            add(static_cast<uint64_t>(connect_manager));
            add(static_cast<uint64_t>(active_connect));
            add(static_cast<uint64_t>(active_transport));
            add(static_cast<uint64_t>(session_connection));
            return value ? value : 1;
        }

        constexpr bool operator==(
            const RollbackSteamSessionIdentity& other) const noexcept
        {
            return lobby_id == other.lobby_id
                && owner_steam_id == other.owner_steam_id
                && local_steam_id == other.local_steam_id
                && remote_steam_id == other.remote_steam_id
                && selection_hash == other.selection_hash
                && same_native_epoch(other)
                && active_connect_state == other.active_connect_state
                && active_connect_sub_state == other.active_connect_sub_state
                && named_session_valid == other.named_session_valid
                && transport_connected == other.transport_connected
                && native_terminal == other.native_terminal
                && terminal_evidence == other.terminal_evidence;
        }
    };

    enum RollbackSteamSessionIdentityConflict : uint32_t
    {
        RollbackSteamIdentityConflictNone = 0,
        RollbackSteamIdentityConflictLobby = 1u << 0,
        RollbackSteamIdentityConflictOwner = 1u << 1,
        RollbackSteamIdentityConflictLocal = 1u << 2,
        RollbackSteamIdentityConflictRemote = 1u << 3,
        RollbackSteamIdentityConflictSelection = 1u << 4,
        RollbackSteamIdentityConflictLifecycle = 1u << 5,
        RollbackSteamIdentityConflictNamedSession = 1u << 6,
        RollbackSteamIdentityConflictSessionInfo = 1u << 7,
        RollbackSteamIdentityConflictConnectManager = 1u << 8,
        RollbackSteamIdentityConflictActiveConnect = 1u << 9,
        RollbackSteamIdentityConflictActiveTransport = 1u << 10,
        RollbackSteamIdentityConflictSessionConnection = 1u << 11,
    };

    static constexpr uint32_t RollbackSteamStablePeerConflictMask(
        const RollbackSteamSessionIdentity& accepted,
        const RollbackSteamSessionIdentity& observed) noexcept
    {
        if (!accepted.stable_peer_valid())
            return RollbackSteamIdentityConflictNone;
        uint32_t mask = RollbackSteamIdentityConflictNone;
        if (observed.lobby_id != 0
            && observed.lobby_id != accepted.lobby_id)
            mask |= RollbackSteamIdentityConflictLobby;
        if (observed.owner_steam_id != 0
            && observed.owner_steam_id != accepted.owner_steam_id)
            mask |= RollbackSteamIdentityConflictOwner;
        if (observed.local_steam_id != 0
            && observed.local_steam_id != accepted.local_steam_id)
            mask |= RollbackSteamIdentityConflictLocal;
        if (observed.remote_steam_id != 0
            && observed.remote_steam_id != accepted.remote_steam_id)
            mask |= RollbackSteamIdentityConflictRemote;
        if (observed.selection_hash != 0
            && observed.selection_hash != accepted.selection_hash)
            mask |= RollbackSteamIdentityConflictSelection;
        return mask;
    }

    static constexpr uint32_t RollbackSteamNativeEpochConflictMask(
        const RollbackSteamSessionIdentity& accepted,
        const RollbackSteamSessionIdentity& observed) noexcept
    {
        if (!accepted.native_epoch_complete())
            return RollbackSteamIdentityConflictNone;
        uint32_t mask = RollbackSteamIdentityConflictNone;
        if (observed.lifecycle_serial != 0
            && observed.lifecycle_serial != accepted.lifecycle_serial)
            mask |= RollbackSteamIdentityConflictLifecycle;
        if (observed.named_session != 0
            && observed.named_session != accepted.named_session)
            mask |= RollbackSteamIdentityConflictNamedSession;
        if (observed.session_info != 0
            && observed.session_info != accepted.session_info)
            mask |= RollbackSteamIdentityConflictSessionInfo;
        if (observed.connect_manager != 0
            && observed.connect_manager != accepted.connect_manager)
            mask |= RollbackSteamIdentityConflictConnectManager;
        if (observed.active_connect != 0
            && observed.active_connect != accepted.active_connect)
            mask |= RollbackSteamIdentityConflictActiveConnect;
        if (observed.active_transport != 0
            && observed.active_transport != accepted.active_transport)
            mask |= RollbackSteamIdentityConflictActiveTransport;
        if (observed.session_connection != 0
            && observed.session_connection != accepted.session_connection)
            mask |= RollbackSteamIdentityConflictSessionConnection;
        return mask;
    }

    static constexpr uint32_t RollbackSteamSessionIdentityConflictMask(
        const RollbackSteamSessionIdentity& accepted,
        const RollbackSteamSessionIdentity& observed) noexcept
    {
        return RollbackSteamStablePeerConflictMask(accepted, observed)
            | RollbackSteamNativeEpochConflictMask(accepted, observed);
    }

    static constexpr bool RollbackSteamSessionIdentityConflicts(
        const RollbackSteamSessionIdentity& accepted,
        const RollbackSteamSessionIdentity& observed) noexcept
    {
        return RollbackSteamSessionIdentityConflictMask(
            accepted, observed) != RollbackSteamIdentityConflictNone;
    }

    static constexpr bool RollbackSteamObservationBelongsToEpoch(
        const RollbackSteamSessionIdentity& accepted,
        const RollbackSteamSessionIdentity& observed) noexcept
    {
        // Teardown may clear any native pointer before Horse samples it. The
        // harness-owned lifecycle serial is therefore the mandatory anchor;
        // every still-present pointer must agree with the accepted lease.
        return accepted.native_epoch_complete()
            && observed.lifecycle_serial != 0
            && observed.lifecycle_serial == accepted.lifecycle_serial
            && RollbackSteamNativeEpochConflictMask(accepted, observed)
                == RollbackSteamIdentityConflictNone;
    }

    enum class RollbackSteamObservationDecision : uint8_t
    {
        WaitForInitialReady = 0,
        AcceptInitialEpoch = 1,
        RejectInitialTerminal = 2,
        PreserveTransientGap = 3,
        RefreshSameEpoch = 4,
        RejectStableIdentityChange = 5,
        RejectNativeEpochChange = 6,
        RevokeTerminalEpoch = 7,
    };

    static constexpr RollbackSteamObservationDecision
    DecideRollbackSteamObservation(
        const RollbackSteamSessionIdentity& accepted,
        const RollbackSteamSessionIdentity& observed) noexcept
    {
        if (!accepted.valid())
        {
            if (observed.native_terminal
                && observed.lifecycle_serial != 0)
            {
                return RollbackSteamObservationDecision
                    ::RejectInitialTerminal;
            }
            return observed.valid()
                ? RollbackSteamObservationDecision::AcceptInitialEpoch
                : RollbackSteamObservationDecision::WaitForInitialReady;
        }

        if (RollbackSteamStablePeerConflictMask(accepted, observed)
            != RollbackSteamIdentityConflictNone)
        {
            return RollbackSteamObservationDecision
                ::RejectStableIdentityChange;
        }
        if (RollbackSteamNativeEpochConflictMask(accepted, observed)
            != RollbackSteamIdentityConflictNone)
        {
            return RollbackSteamObservationDecision
                ::RejectNativeEpochChange;
        }
        const bool belongs = RollbackSteamObservationBelongsToEpoch(
            accepted, observed);
        if (observed.native_terminal)
        {
            return belongs
                ? RollbackSteamObservationDecision::RevokeTerminalEpoch
                : RollbackSteamObservationDecision
                    ::RejectNativeEpochChange;
        }
        if (!belongs || !observed.native_epoch_complete()
            || !accepted.same_native_epoch(observed))
        {
            return RollbackSteamObservationDecision
                ::RejectNativeEpochChange;
        }
        if (!observed.valid())
        {
            // Only readiness booleans may flicker without changing the
            // complete native object graph.
            return RollbackSteamObservationDecision
                ::PreserveTransientGap;
        }
        return RollbackSteamObservationDecision::RefreshSameEpoch;
    }

    enum class RollbackSessionContractStage : uint8_t
    {
        None = 0,
        Hello = 1,
        Accepted = 2,
    };

    static constexpr uint8_t kRollbackSessionContractVersion = 2;

#pragma pack(push, 1)
    struct RollbackSessionContractMessage
    {
        uint8_t version {kRollbackSessionContractVersion};
        RollbackSessionContractStage stage {
            RollbackSessionContractStage::None};
        uint8_t local_player_slot {0};
        uint8_t local_is_host {0};
        uint16_t rollback_window {0};
        uint16_t input_delay {0};
        RollbackSavePolicy save_policy {
            RollbackSavePolicy::ConfirmedSpeculative};
        uint8_t lead_pacing_enabled {1};
        uint16_t lead_pacing_enter_milliframes {1500};
        uint16_t lead_pacing_exit_milliframes {500};
        uint8_t lead_pacing_maximum_holds {2};
        uint64_t lobby_id {0};
        uint64_t owner_steam_id {0};
        uint64_t local_steam_id {0};
        uint64_t remote_steam_id {0};
        uint64_t selection_hash {0};
        uint64_t host_nonce {0};
        uint64_t build_id {0};
        uint64_t schema_id {0};
        uint64_t contract_hash {0};
        uint64_t session_epoch {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackSessionContractMessage) == 95);

    static constexpr bool RollbackSessionContractValid(
        const RollbackSessionContractMessage& message) noexcept
    {
        const bool accepted = message.stage
            == RollbackSessionContractStage::Accepted;
        return message.version == kRollbackSessionContractVersion
            && (message.stage == RollbackSessionContractStage::Hello
                || accepted)
            && message.local_player_slot < 2
            && message.local_is_host < 2
            && message.rollback_window != 0
            && message.input_delay <= message.rollback_window
            && RollbackSavePolicyValid(message.save_policy)
            && message.lead_pacing_enabled < 2
            && message.lead_pacing_enter_milliframes != 0
            && message.lead_pacing_exit_milliframes
                < message.lead_pacing_enter_milliframes
            && message.lead_pacing_maximum_holds != 0
            && message.lead_pacing_maximum_holds <= 8
            && message.lobby_id != 0
            && message.owner_steam_id != 0
            && message.local_steam_id != 0
            && message.remote_steam_id != 0
            && message.local_steam_id != message.remote_steam_id
            && (message.owner_steam_id == message.local_steam_id
                || message.owner_steam_id == message.remote_steam_id)
            && message.selection_hash != 0
            && message.build_id != 0 && message.schema_id != 0
            && message.contract_hash != 0
            && (!accepted
                || (message.host_nonce != 0
                    && message.session_epoch != 0));
    }

    static constexpr bool RollbackSessionContractsMatch(
        const RollbackSessionContractMessage& local,
        const RollbackSessionContractMessage& remote) noexcept
    {
        return RollbackSessionContractValid(local)
            && RollbackSessionContractValid(remote)
            && local.local_player_slot != remote.local_player_slot
            && local.local_is_host != remote.local_is_host
            && local.lobby_id == remote.lobby_id
            && local.owner_steam_id == remote.owner_steam_id
            && local.local_steam_id == remote.remote_steam_id
            && local.remote_steam_id == remote.local_steam_id
            && local.selection_hash == remote.selection_hash
            && local.rollback_window == remote.rollback_window
            && local.input_delay == remote.input_delay
            && local.save_policy == remote.save_policy
            && local.lead_pacing_enabled == remote.lead_pacing_enabled
            && local.lead_pacing_enter_milliframes
                == remote.lead_pacing_enter_milliframes
            && local.lead_pacing_exit_milliframes
                == remote.lead_pacing_exit_milliframes
            && local.lead_pacing_maximum_holds
                == remote.lead_pacing_maximum_holds
            && local.build_id == remote.build_id
            && local.schema_id == remote.schema_id
            && local.contract_hash == remote.contract_hash
            && local.host_nonce != 0
            && local.host_nonce == remote.host_nonce
            && local.session_epoch != 0
            && local.session_epoch == remote.session_epoch;
    }

    enum class RollbackLaunchBarrierStage : uint8_t
    {
        None = 0,
        BattleBaseline = 1,
        BattleBaselineAccepted = 2,
    };

    static constexpr bool RollbackLaunchBaselineVerificationRequired(
        bool local_valid, RollbackLaunchBarrierStage local_stage) noexcept
    {
        return local_valid
            && static_cast<uint8_t>(local_stage)
                >= static_cast<uint8_t>(
                    RollbackLaunchBarrierStage::BattleBaseline);
    }

    static constexpr uint8_t kRollbackLaunchBarrierVersion = 10;

#pragma pack(push, 1)
    struct RollbackLaunchBarrierMessage
    {
        uint8_t version {kRollbackLaunchBarrierVersion};
        RollbackLaunchBarrierStage stage {RollbackLaunchBarrierStage::None};
        RollbackLifecycleMode lifecycle_mode {
            RollbackLifecycleMode::StockOnlinePvp};
        uint8_t local_player_slot {0};
        uint32_t logical_frame {0};
        int32_t native_boundary_frame {-1};
        uint32_t round_ordinal {0};
        uint32_t input_log_frame {0};
        uint32_t completed_round_ordinal {0xFFFFFFFFu};
        uint32_t replay_round_index {0};
        uint64_t canonical_stage_identity {0};
        uint64_t session_epoch {0};
        uint64_t round_generation {0};
        uint64_t match_identity_digest {0};
        uint64_t replay_digest {0};
        uint64_t round_identity_digest {0};
        uint64_t lifecycle_digest {0};
        uint64_t epoch {0};
        uint64_t precontrol_identity_digest {0};
        uint64_t canonical_baseline_hash {0};
        uint64_t component_hash[4] {};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackLaunchBarrierMessage) == 140);
    using RollbackRoundBaselineMessage = RollbackLaunchBarrierMessage;

    static constexpr bool RollbackLaunchBarrierValid(
        const RollbackLaunchBarrierMessage& message) noexcept
    {
        if (message.version != kRollbackLaunchBarrierVersion
            || !RollbackLifecycleModeValid(message.lifecycle_mode)
            || message.local_player_slot >= 2)
        {
            return false;
        }
        const bool baseline_stage = message.stage
                == RollbackLaunchBarrierStage::BattleBaseline
            || message.stage
                == RollbackLaunchBarrierStage::BattleBaselineAccepted;
        const bool common_valid = baseline_stage
            && message.logical_frame == 0
            && message.native_boundary_frame >= 0
            && message.canonical_stage_identity != 0
            && message.session_epoch != 0
            && message.round_generation != 0
            && message.match_identity_digest != 0
            && message.replay_digest != 0
            && message.round_identity_digest != 0
            && message.lifecycle_digest != 0
            && message.epoch != 0
            && message.precontrol_identity_digest != 0;
        if (!common_valid) return false;
        return message.canonical_baseline_hash != 0
            && message.component_hash[0] != 0
            && message.component_hash[1] != 0;
    }

    static constexpr bool RollbackLaunchBarriersMatch(
        const RollbackLaunchBarrierMessage& local,
        const RollbackLaunchBarrierMessage& remote) noexcept
    {
        if (!RollbackLaunchBarrierValid(local)
            || !RollbackLaunchBarrierValid(remote)
            || local.local_player_slot == remote.local_player_slot
            || local.lifecycle_mode != remote.lifecycle_mode
            || local.canonical_stage_identity
                != remote.canonical_stage_identity)
        {
            return false;
        }
        const bool common_identity =
                local.round_ordinal == remote.round_ordinal
                && local.completed_round_ordinal
                    == remote.completed_round_ordinal
                && local.replay_round_index == remote.replay_round_index
                && local.session_epoch == remote.session_epoch
                && local.round_generation == remote.round_generation
                && local.match_identity_digest
                    == remote.match_identity_digest
                && local.replay_digest == remote.replay_digest
                && local.precontrol_identity_digest
                    == remote.precontrol_identity_digest;
        if (!common_identity) return false;
        const bool local_baseline =
            static_cast<uint8_t>(local.stage)
                >= static_cast<uint8_t>(
                    RollbackLaunchBarrierStage::BattleBaseline);
        const bool remote_baseline =
            static_cast<uint8_t>(remote.stage)
                >= static_cast<uint8_t>(
                    RollbackLaunchBarrierStage::BattleBaseline);
        if (!local_baseline || !remote_baseline) return true;
        return local_baseline && remote_baseline
                && local.round_identity_digest
                    == remote.round_identity_digest
                // StockOnline's InputLog clock is peer-local and can differ
                // while both clients are on the same native simulation frame.
                // The canonical state hash is the equality proof.
                && local.lifecycle_digest == remote.lifecycle_digest
                && local.epoch == remote.epoch
                && local.canonical_baseline_hash
                    == remote.canonical_baseline_hash
                && local.component_hash[0] == remote.component_hash[0]
                && local.component_hash[1] == remote.component_hash[1]
                && local.component_hash[2] == remote.component_hash[2]
                && local.component_hash[3] == remote.component_hash[3];
    }

    static constexpr bool RollbackLaunchBarrierIsStaleDuplicate(
        const RollbackLaunchBarrierMessage& current,
        const RollbackLaunchBarrierMessage& candidate) noexcept
    {
        return RollbackLaunchBarrierValid(current)
            && RollbackLaunchBarrierValid(candidate)
            && current.lifecycle_mode
                == RollbackLifecycleMode::StockOnlinePvp
            && static_cast<uint8_t>(candidate.stage)
                < static_cast<uint8_t>(current.stage)
            && current.local_player_slot == candidate.local_player_slot
            && current.logical_frame == candidate.logical_frame
            && current.native_boundary_frame
                == candidate.native_boundary_frame
            && current.round_ordinal == candidate.round_ordinal
            && current.input_log_frame == candidate.input_log_frame
            && current.completed_round_ordinal
                == candidate.completed_round_ordinal
            && current.replay_round_index == candidate.replay_round_index
            && current.canonical_stage_identity
                == candidate.canonical_stage_identity
            && current.session_epoch == candidate.session_epoch
            && current.round_generation == candidate.round_generation
            && current.match_identity_digest
                == candidate.match_identity_digest
            && current.replay_digest == candidate.replay_digest
            && current.round_identity_digest
                == candidate.round_identity_digest
            && current.lifecycle_digest == candidate.lifecycle_digest
            && current.epoch == candidate.epoch
            && current.precontrol_identity_digest
                == candidate.precontrol_identity_digest
            && current.canonical_baseline_hash
                == candidate.canonical_baseline_hash
            && current.component_hash[0] == candidate.component_hash[0]
            && current.component_hash[1] == candidate.component_hash[1]
            && current.component_hash[2] == candidate.component_hash[2]
            && current.component_hash[3] == candidate.component_hash[3];
    }

    static inline bool RollbackLaunchBarrierSameSenderPayload(
        const RollbackLaunchBarrierMessage& current,
        const RollbackLaunchBarrierMessage& candidate) noexcept
    {
        RollbackLaunchBarrierMessage normalized = candidate;
        normalized.stage = current.stage;
        return RollbackLaunchBarrierValid(current)
            && RollbackLaunchBarrierValid(candidate)
            && std::memcmp(&current, &normalized, sizeof(current)) == 0;
    }

    enum class RollbackLaunchBarrierInboxDisposition : uint8_t
    {
        Stored,
        Idempotent,
        Stale,
        Invalid,
        Conflict,
    };

    class RollbackLaunchBarrierInbox
    {
    public:
        void reset() noexcept { *this = {}; }

        bool configure(
            uint8_t local_player_slot,
            uint64_t session_epoch,
            uint64_t handshake_generation) noexcept
        {
            if (local_player_slot >= 2 || session_epoch == 0
                || handshake_generation == 0)
            {
                return false;
            }
            reset();
            m_local_player_slot = local_player_slot;
            m_session_epoch = session_epoch;
            m_handshake_generation = handshake_generation;
            m_configured = true;
            return true;
        }

        bool configured_for(
            uint8_t local_player_slot,
            uint64_t session_epoch,
            uint64_t handshake_generation) const noexcept
        {
            return m_configured
                && m_local_player_slot == local_player_slot
                && m_session_epoch == session_epoch
                && m_handshake_generation == handshake_generation;
        }

        RollbackLaunchBarrierInboxDisposition observe_local(
            const RollbackLaunchBarrierMessage& message,
            uint64_t handshake_generation) noexcept
        {
            return observe(
                m_local, m_local_valid, message, handshake_generation,
                m_local_player_slot);
        }

        RollbackLaunchBarrierInboxDisposition accept_peer(
            const RollbackLaunchBarrierMessage& message,
            uint64_t handshake_generation) noexcept
        {
            return observe(
                m_peer, m_peer_valid, message, handshake_generation,
                static_cast<uint8_t>(1u - m_local_player_slot));
        }

        bool ready(RollbackLaunchBarrierStage stage) const noexcept
        {
            return m_local_valid && m_peer_valid
                && static_cast<uint8_t>(m_local.stage)
                    >= static_cast<uint8_t>(stage)
                && static_cast<uint8_t>(m_peer.stage)
                    >= static_cast<uint8_t>(stage)
                && RollbackLaunchBarriersMatch(m_local, m_peer);
        }

        bool local_valid() const noexcept { return m_local_valid; }
        bool peer_valid() const noexcept { return m_peer_valid; }
        const RollbackLaunchBarrierMessage& local() const noexcept
        {
            return m_local;
        }
        const RollbackLaunchBarrierMessage& peer() const noexcept
        {
            return m_peer;
        }

    private:
        RollbackLaunchBarrierInboxDisposition observe(
            RollbackLaunchBarrierMessage& current,
            bool& current_valid,
            const RollbackLaunchBarrierMessage& candidate,
            uint64_t handshake_generation,
            uint8_t expected_slot) noexcept
        {
            if (!m_configured
                || handshake_generation != m_handshake_generation
                || !RollbackLaunchBarrierValid(candidate)
                || candidate.local_player_slot != expected_slot
                || candidate.session_epoch != m_session_epoch)
            {
                return RollbackLaunchBarrierInboxDisposition::Invalid;
            }
            if (!current_valid)
            {
                current = candidate;
                current_valid = true;
                return RollbackLaunchBarrierInboxDisposition::Stored;
            }
            if (!RollbackLaunchBarrierSameSenderPayload(current, candidate))
                return RollbackLaunchBarrierInboxDisposition::Conflict;
            const uint8_t current_stage =
                static_cast<uint8_t>(current.stage);
            const uint8_t candidate_stage =
                static_cast<uint8_t>(candidate.stage);
            if (candidate_stage < current_stage)
                return RollbackLaunchBarrierInboxDisposition::Stale;
            if (candidate_stage == current_stage)
                return RollbackLaunchBarrierInboxDisposition::Idempotent;
            current = candidate;
            return RollbackLaunchBarrierInboxDisposition::Stored;
        }

        RollbackLaunchBarrierMessage m_local {};
        RollbackLaunchBarrierMessage m_peer {};
        uint64_t m_session_epoch {0};
        uint64_t m_handshake_generation {0};
        uint8_t m_local_player_slot {0};
        bool m_configured {false};
        bool m_local_valid {false};
        bool m_peer_valid {false};
    };

    enum class RollbackRoundTransitionBarrierStage : uint8_t
    {
        None = 0,
        Proposed = 1,
        Restored = 2,
        Accepted = 3,
    };

    static constexpr uint8_t kRollbackRoundTransitionBarrierVersion = 3;

#pragma pack(push, 1)
    struct RollbackRoundTransitionBarrierMessage
    {
        uint8_t version {kRollbackRoundTransitionBarrierVersion};
        RollbackRoundTransitionBarrierStage stage {
            RollbackRoundTransitionBarrierStage::None};
        uint8_t local_player_slot {0};
        uint8_t reserved {0};
        uint32_t confirmed_frame {0};
        // Earliest frame whose bilateral comparison/ACK transaction was not
        // complete when this peer froze its terminal proposal. Peers may
        // advertise different starts; both retransmit from the earlier one
        // so a locally restored peer cannot strand the other in Proposed.
        uint32_t tail_start_frame {0};
        uint32_t completed_round_ordinal {0};
        uint64_t pair_epoch {0};
        uint64_t canonical_hash {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackRoundTransitionBarrierMessage) == 32);

    static constexpr bool RollbackRoundTransitionBarrierValid(
        const RollbackRoundTransitionBarrierMessage& message) noexcept
    {
        return message.version == kRollbackRoundTransitionBarrierVersion
            && (message.stage
                    == RollbackRoundTransitionBarrierStage::Proposed
                || message.stage
                    == RollbackRoundTransitionBarrierStage::Restored
                || message.stage
                    == RollbackRoundTransitionBarrierStage::Accepted)
            && message.local_player_slot < 2
            && message.reserved == 0
            && message.tail_start_frame <= message.confirmed_frame
            && message.pair_epoch != 0
            && message.canonical_hash != 0;
    }

    static constexpr bool RollbackRoundTransitionBarrierSessionMatches(
        const RollbackRoundTransitionBarrierMessage& local,
        const RollbackRoundTransitionBarrierMessage& remote) noexcept
    {
        return RollbackRoundTransitionBarrierValid(local)
            && RollbackRoundTransitionBarrierValid(remote)
            && local.local_player_slot != remote.local_player_slot
            && local.completed_round_ordinal
                == remote.completed_round_ordinal
            && local.pair_epoch == remote.pair_epoch;
    }

    static constexpr bool RollbackRoundTransitionBarrierIdentityEqual(
        const RollbackRoundTransitionBarrierMessage& lhs,
        const RollbackRoundTransitionBarrierMessage& rhs) noexcept
    {
        return RollbackRoundTransitionBarrierValid(lhs)
            && RollbackRoundTransitionBarrierValid(rhs)
            && lhs.local_player_slot == rhs.local_player_slot
            && lhs.confirmed_frame == rhs.confirmed_frame
            && lhs.tail_start_frame == rhs.tail_start_frame
            && lhs.completed_round_ordinal == rhs.completed_round_ordinal
            && lhs.pair_epoch == rhs.pair_epoch
            && lhs.canonical_hash == rhs.canonical_hash;
    }

    static constexpr bool RollbackRoundTransitionBarriersMatch(
        const RollbackRoundTransitionBarrierMessage& local,
        const RollbackRoundTransitionBarrierMessage& remote) noexcept
    {
        return RollbackRoundTransitionBarrierSessionMatches(local, remote)
            && local.confirmed_frame == remote.confirmed_frame
            && local.canonical_hash == remote.canonical_hash;
    }

    enum class RollbackRoundTerminalProposalAction : uint8_t
    {
        WaitForLocalFrame = 0,
        KeepLocal = 1,
        AdoptRemote = 2,
        Agreed = 3,
        Reject = 4,
    };

    static constexpr bool RollbackRoundLogicalFrameAfter(
        uint32_t lhs, uint32_t rhs) noexcept
    {
        return static_cast<int32_t>(lhs - rhs) > 0;
    }

    static constexpr bool RollbackRoundTransitionTailStart(
        const RollbackRoundTransitionBarrierMessage& local,
        const RollbackRoundTransitionBarrierMessage& remote,
        uint32_t& out) noexcept
    {
        if (!RollbackRoundTransitionBarriersMatch(local, remote))
            return false;
        out = RollbackRoundLogicalFrameAfter(
                local.tail_start_frame, remote.tail_start_frame)
            ? remote.tail_start_frame : local.tail_start_frame;
        return !RollbackRoundLogicalFrameAfter(
            out, local.confirmed_frame);
    }

    static constexpr RollbackRoundTerminalProposalAction
    ClassifyRollbackRoundTerminalProposal(
        const RollbackRoundTransitionBarrierMessage& local,
        const RollbackRoundTransitionBarrierMessage& remote,
        uint32_t local_confirmed_frame,
        bool remote_checkpoint_matches) noexcept
    {
        if (local.stage
                != RollbackRoundTransitionBarrierStage::Proposed
            || !RollbackRoundTransitionBarrierSessionMatches(local, remote))
        {
            return RollbackRoundTerminalProposalAction::Reject;
        }
        if (local.confirmed_frame == remote.confirmed_frame)
        {
            return local.canonical_hash == remote.canonical_hash
                ? RollbackRoundTerminalProposalAction::Agreed
                : RollbackRoundTerminalProposalAction::Reject;
        }
        if (remote.stage
            != RollbackRoundTransitionBarrierStage::Proposed)
        {
            return RollbackRoundTerminalProposalAction::Reject;
        }
        if (!RollbackRoundLogicalFrameAfter(
                remote.confirmed_frame, local.confirmed_frame))
        {
            return RollbackRoundTerminalProposalAction::KeepLocal;
        }
        if (RollbackRoundLogicalFrameAfter(
                remote.confirmed_frame, local_confirmed_frame)
            || !remote_checkpoint_matches)
        {
            return RollbackRoundTerminalProposalAction::WaitForLocalFrame;
        }
        return RollbackRoundTerminalProposalAction::AdoptRemote;
    }

    static constexpr bool RollbackRoundTransitionBarrierComplete(
        const RollbackRoundTransitionBarrierMessage& local,
        const RollbackRoundTransitionBarrierMessage& remote) noexcept
    {
        return RollbackRoundTransitionBarriersMatch(local, remote)
            && local.stage
                == RollbackRoundTransitionBarrierStage::Accepted
            && remote.stage
                == RollbackRoundTransitionBarrierStage::Accepted;
    }

    // Seeing both Accepted barriers does not prove that this peer's final UDP
    // datagram reached the other side. Retain and periodically republish the
    // immutable identity from a service clock while gameplay is frozen.
    static constexpr bool RollbackRoundTransitionAcceptedRepublishDue(
        const RollbackRoundTransitionBarrierMessage& local,
        bool local_valid,
        bool terminal_identity_retained,
        bool peer_next_round_progress,
        uint64_t service_tick,
        uint32_t period = 6) noexcept
    {
        return local_valid
            && terminal_identity_retained
            && !peer_next_round_progress
            && period != 0
            && service_tick % period == 0
            && RollbackRoundTransitionBarrierValid(local)
            && local.stage
                == RollbackRoundTransitionBarrierStage::Accepted;
    }

    enum class RollbackPreNewRoundBarrierStage : uint8_t
    {
        None = 0,
        Ready = 1,
        Accepted = 2,
    };

    static constexpr uint8_t kRollbackPreNewRoundBarrierVersion = 1;

#pragma pack(push, 1)
    struct RollbackPreNewRoundBarrierMessage
    {
        uint8_t version {kRollbackPreNewRoundBarrierVersion};
        RollbackPreNewRoundBarrierStage stage {
            RollbackPreNewRoundBarrierStage::None};
        uint8_t local_player_slot {0};
        uint8_t reserved0 {0};
        uint32_t completed_round_ordinal {0};
        uint32_t target_round_ordinal {0};
        uint64_t session_epoch {0};
        uint64_t completed_pair_epoch {0};
        uint64_t terminal_canonical_hash {0};
        uint64_t target_round_generation {0};
        uint64_t match_identity_digest {0};
        uint64_t entry_digest {0};
        uint32_t native_stage_identity {0};
        uint32_t reserved1 {0};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackPreNewRoundBarrierMessage) == 68);

    static constexpr bool RollbackPreNewRoundBarrierValid(
        const RollbackPreNewRoundBarrierMessage& message) noexcept
    {
        return message.version == kRollbackPreNewRoundBarrierVersion
            && (message.stage == RollbackPreNewRoundBarrierStage::Ready
                || message.stage
                    == RollbackPreNewRoundBarrierStage::Accepted)
            && message.local_player_slot < 2
            && message.reserved0 == 0
            && message.reserved1 == 0
            && message.target_round_ordinal
                == ((message.completed_round_ordinal + 1u) & 0xFFFFu)
            && message.session_epoch != 0
            && message.completed_pair_epoch != 0
            && message.terminal_canonical_hash != 0
            && message.target_round_generation > 1
            && message.match_identity_digest != 0
            && message.entry_digest != 0
            && message.native_stage_identity != 0;
    }

    static constexpr bool RollbackPreNewRoundBarriersMatch(
        const RollbackPreNewRoundBarrierMessage& local,
        const RollbackPreNewRoundBarrierMessage& remote) noexcept
    {
        return RollbackPreNewRoundBarrierValid(local)
            && RollbackPreNewRoundBarrierValid(remote)
            && local.local_player_slot != remote.local_player_slot
            && local.completed_round_ordinal
                == remote.completed_round_ordinal
            && local.target_round_ordinal == remote.target_round_ordinal
            && local.session_epoch == remote.session_epoch
            && local.completed_pair_epoch == remote.completed_pair_epoch
            && local.terminal_canonical_hash
                == remote.terminal_canonical_hash
            && local.target_round_generation
                == remote.target_round_generation
            && local.match_identity_digest == remote.match_identity_digest
            && local.entry_digest == remote.entry_digest
            && local.native_stage_identity == remote.native_stage_identity;
    }

    static constexpr bool RollbackPreNewRoundBarrierSamePeerIdentity(
        const RollbackPreNewRoundBarrierMessage& first,
        const RollbackPreNewRoundBarrierMessage& second) noexcept
    {
        return RollbackPreNewRoundBarrierValid(first)
            && RollbackPreNewRoundBarrierValid(second)
            && first.local_player_slot == second.local_player_slot
            && first.completed_round_ordinal
                == second.completed_round_ordinal
            && first.target_round_ordinal == second.target_round_ordinal
            && first.session_epoch == second.session_epoch
            && first.completed_pair_epoch == second.completed_pair_epoch
            && first.terminal_canonical_hash
                == second.terminal_canonical_hash
            && first.target_round_generation
                == second.target_round_generation
            && first.match_identity_digest == second.match_identity_digest
            && first.entry_digest == second.entry_digest
            && first.native_stage_identity == second.native_stage_identity;
    }

    static constexpr bool RollbackPreNewRoundBarrierComplete(
        const RollbackPreNewRoundBarrierMessage& local,
        const RollbackPreNewRoundBarrierMessage& remote) noexcept
    {
        return RollbackPreNewRoundBarriersMatch(local, remote)
            && local.stage == RollbackPreNewRoundBarrierStage::Accepted
            && remote.stage == RollbackPreNewRoundBarrierStage::Accepted;
    }
}
