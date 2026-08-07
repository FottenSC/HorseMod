// ============================================================================
// Horse::RollbackReplayForkRuntime
//
// Test-only two-process replay fixture. It owns no menu/gameflow automation
// and cannot activate a production lifecycle. The runtime feeds deterministic
// replay inputs through RollbackGekkoRuntimeCore and the same full snapshot
// Save/Load/Advance contract used by production.
// ============================================================================

#pragma once

#include "ReplayScrub.hpp"
#include "RollbackGekkoGameplayInputBridge.hpp"
#include "RollbackGekkoRuntimeCore.hpp"
#include "RollbackReplayInputScript.hpp"
#include "RollbackSnapshotStore.hpp"
#include "RollbackStateHash.hpp"
#include "RollbackStepHarness.hpp"
#include "RollbackUdpRuntime.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Horse
{
    enum class RollbackReplayForkState : uint8_t
    {
        Disabled,
        WaitingForAnchor,
        SettlingHold,
        Stability,
        DirectStepMatrix,
        WaitingForPeer,
        WaitingForGekko,
        Running,
        WaitingForFinalConsensus,
        VerifyingResume,
        Complete,
        Fatal,
    };

    static constexpr const char* RollbackReplayForkStateName(
        RollbackReplayForkState state) noexcept
    {
        switch (state)
        {
        case RollbackReplayForkState::Disabled: return "disabled";
        case RollbackReplayForkState::WaitingForAnchor:
            return "waiting-for-anchor";
        case RollbackReplayForkState::SettlingHold: return "settling-hold";
        case RollbackReplayForkState::Stability: return "stability";
        case RollbackReplayForkState::DirectStepMatrix:
            return "direct-step-matrix";
        case RollbackReplayForkState::WaitingForPeer:
            return "waiting-for-peer";
        case RollbackReplayForkState::WaitingForGekko:
            return "waiting-for-gekko";
        case RollbackReplayForkState::Running: return "running";
        case RollbackReplayForkState::WaitingForFinalConsensus:
            return "waiting-for-final-consensus";
        case RollbackReplayForkState::VerifyingResume:
            return "verifying-resume";
        case RollbackReplayForkState::Complete: return "complete";
        case RollbackReplayForkState::Fatal: return "fatal";
        }
        return "unknown";
    }

    struct RollbackReplayForkConfig
    {
        bool enabled {false};
        RollbackProductionConfig transport {};
        std::string replay_input_file;
        std::string request_id;
        std::string client_role;
        uint32_t stability_ticks {120};
        uint32_t run_frames {600};
        bool require_rollback {false};

        bool valid() const noexcept
        {
            return enabled && transport.valid()
                && transport.session_domain
                    == RollbackSessionDomain::ReplayForkLab
                && !replay_input_file.empty()
                && !request_id.empty()
                && (client_role == "host" || client_role == "sandbox")
                && stability_ticks >= 120
                && run_frames >= 60
                && run_frames <= 60u * 300u;
        }
    };

    struct RollbackReplayForkStatus
    {
        RollbackReplayForkState state {RollbackReplayForkState::Disabled};
        const char* failure {"disabled"};
        uint64_t service_ticks {0};
        uint64_t executable_id {0};
        uint64_t baseline_hash {0};
        uint64_t final_hash {0};
        uint64_t peer_final_hash {0};
        uint64_t local_input_hash {0};
        uint64_t remote_input_hash {0};
        uint64_t saves {0};
        uint64_t loads {0};
        uint64_t advances {0};
        uint64_t rollback_advances {0};
        uint64_t pair_accepts {0};
        uint64_t terminal_evidence_hash {0};
        uint64_t presentation_syncs {0};
        uint64_t presentation_skips {0};
        uint64_t presentation_failures {0};
        uint32_t completed_direct_windows {0};
        uint32_t completed_frames {0};
        uint32_t evidence_frames {0};
        uint32_t summary_overwrites {0};
        uint32_t snapshot_peak {0};
        bool hold_stable {false};
        bool baseline_restored {false};
        bool replay_resumed {false};
        bool gekko_started {false};
        bool prediction_diverged {false};
        bool no_desync {true};
        bool presentation_gameplay_unchanged {true};
        bool presentation_motion_observed {false};
    };

#pragma pack(push, 1)
    struct RollbackReplayForkFrameSummary
    {
        uint32_t magic {0x324C4652u}; // RFL2
        uint32_t frame {0};
        uint64_t pair_epoch {0};
        uint64_t canonical_hash {0};
        uint32_t input[2] {};
        uint64_t owned_input_hash {0};
        uint32_t owned_input_count {0};
        uint8_t owned_player_slot {0};
        uint8_t reserved[3] {};
    };
    struct RollbackReplayForkBaselineProof
    {
        uint32_t magic {0x31424652u}; // RFB1
        uint32_t phase {1}; // 1=proof, 2=ack
        int32_t anchor_sequence {0};
        int32_t anchor_round {0};
        int32_t anchor_master {0};
        uint64_t pair_epoch {0};
        uint64_t baseline_hash {0};
    };
    struct RollbackReplayForkTerminalProof
    {
        uint32_t magic {0x31544652u}; // RFT1
        uint32_t phase {1}; // 1=proof, 2=ack
        uint32_t frame {0};
        uint32_t owned_input_count {0};
        uint64_t pair_epoch {0};
        uint64_t canonical_hash {0};
        uint64_t owned_input_hash {0};
        uint8_t owned_player_slot {0};
        uint8_t reserved[7] {};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackReplayForkFrameSummary) == 48);
    static_assert(sizeof(RollbackReplayForkBaselineProof) == 36);
    static_assert(sizeof(RollbackReplayForkTerminalProof) == 48);

    class RollbackReplayForkRuntime
    {
    public:
        static RollbackReplayForkRuntime& instance() noexcept
        {
            static RollbackReplayForkRuntime runtime;
            return runtime;
        }

        void configure(RollbackReplayForkConfig config) noexcept
        {
            shutdown(true);
            m_config = std::move(config);
            m_status = {};
            if (!m_config.valid())
            {
                m_status.state = RollbackReplayForkState::Fatal;
                m_status.failure = "replay-fork-config-invalid";
                return;
            }
            if (!load_input_script())
            {
                m_status.state = RollbackReplayForkState::Fatal;
                m_status.failure = m_script.failure;
                return;
            }
            try
            {
                m_local_summaries.assign(
                    m_config.run_frames, RollbackReplayForkFrameSummary{});
                m_remote_summaries.assign(
                    m_config.run_frames, RollbackReplayForkFrameSummary{});
                m_local_summary_valid.assign(m_config.run_frames, 0);
                m_remote_summary_valid.assign(m_config.run_frames, 0);
            }
            catch (...)
            {
                m_status.state = RollbackReplayForkState::Fatal;
                m_status.failure =
                    "replay-fork-evidence-table-allocation-failed";
                return;
            }
            m_status.state = RollbackReplayForkState::WaitingForAnchor;
            m_status.failure = "waiting-for-anchor";
            emit_status(true);
        }

        void shutdown(bool resume_playback = true) noexcept
        {
            m_gekko.shutdown();
            m_network.stop();
            bool restore_ok = true;
            if (m_anchor_captured)
            {
                const RollbackStepStateReport restored =
                    RestoreRollbackStepState(
                        NativeBinding::imageBase(), m_anchor, true,
                        RollbackLifecycleMode::StockOnlinePvp, true, true,
                        nullptr, nullptr, nullptr,
                        &m_palette_variant_writer_registry);
                restore_ok = restored.ok;
                m_status.baseline_restored = restore_ok;
            }
            bool hold_released = false;
            if (ReplayScrub::instance().replay_fork_lab_hold_active())
            {
                if (restore_ok)
                {
                    ReplayScrub::instance().exit_replay_fork_lab_hold(
                        resume_playback);
                    hold_released = true;
                    m_status.replay_resumed = resume_playback;
                }
            }
            if (!m_config.request_id.empty())
            {
                ReplayTraceFields cleanup;
                append_common(cleanup);
                cleanup.boolean("anchor_restore_ok", restore_ok)
                    .boolean("hold_released", hold_released)
                    .boolean("resume_requested", resume_playback)
                    .boolean("replay_resumed",
                        m_status.replay_resumed);
                ReplayDebugTrace::instance().event(
                    "rollback_replay_fork_cleanup", cleanup);
            }
            m_store.clear();
            m_anchor = {};
            m_anchor_captured = false;
            m_network_started = false;
            m_seek_requested = false;
            m_hold_settle_ticks = 0;
            m_stability_observations = 0;
            m_direct_window_index = 0;
            m_anchor_input_index = 0;
            m_run_input_index = 0;
            m_gekko_bootstrap_submissions = 0;
            m_owned_input_count = 0;
            m_final_frame.clear();
            m_final_summary_accepted = false;
            m_terminal_summary = {};
            m_terminal_summary_valid = false;
            m_terminal_summary_conflict = false;
            m_terminal_evidence_dirty = true;
            m_terminal_proof_sent = false;
            m_terminal_proof_accepted = false;
            m_terminal_ack_accepted = false;
            m_terminal_barrier_complete_tick = 0;
            m_current_frame.clear();
            m_post_advance_state = {};
            m_post_advance_frame.clear();
            m_resume_observation = {};
            m_lab_epoch = {};
            m_local_summaries.clear();
            m_remote_summaries.clear();
            m_local_summary_valid.clear();
            m_remote_summary_valid.clear();
            m_prediction_hash_valid.fill(false);
            m_last_advance_canonical_hash = 0;
            m_last_presentation_hash = 0;
            m_last_presentation_valid = false;
            m_presentation_actor[0] = nullptr;
            m_presentation_actor[1] = nullptr;
            m_presentation_actor_array_index[0] = -1;
            m_presentation_actor_array_index[1] = -1;
            m_presentation_actor_array_count = 0;
            m_actor_location_fn.clear();
            m_lux.invalidate();
            std::memset(m_last_published_transform, 0,
                        sizeof(m_last_published_transform));
            m_baseline_proof_sent = false;
            m_baseline_proof_accepted = false;
            m_baseline_ack_accepted = false;
            m_baseline_barrier_complete_tick = 0;
            m_core_failure_reason = nullptr;
        }

        const RollbackReplayForkStatus& status() const noexcept
        {
            return m_status;
        }

        bool active() const noexcept
        {
            return m_status.state != RollbackReplayForkState::Disabled
                && m_status.state != RollbackReplayForkState::Complete
                && m_status.state != RollbackReplayForkState::Fatal;
        }

        void service_game_thread(
            const RollbackSnapshotManifest& manifest) noexcept
        {
            if (!m_config.enabled
                || m_status.state == RollbackReplayForkState::Disabled
                || m_status.state == RollbackReplayForkState::Complete
                || m_status.state == RollbackReplayForkState::Fatal)
                return;
            ++m_status.service_ticks;
            m_manifest = manifest;

            if (m_status.executable_id == 0)
                m_status.executable_id = ComputeRollbackExecutableId(
                    NativeBinding::imageBase());
            if (m_status.executable_id == 0
                || m_status.executable_id
                    != m_config.transport.expected_build_id)
            {
                fail("replay-fork-build-id-mismatch");
                return;
            }

            if (manifest.schema_hash() != m_config.transport.expected_schema_id)
            {
                fail("replay-fork-schema-id-mismatch");
                return;
            }
            // Claim an auto-armed generator as soon as replay presence is
            // visible. Snapshot-manifest live readiness can lag several
            // thousand fast-generated frames, which is too late to install
            // an exact stop at sequence 2751.
            if (m_status.state == RollbackReplayForkState::WaitingForAnchor)
            {
                (void)ReplayScrub::instance().replay_fork_generate_to_anchor(
                    m_config.transport.replay_anchor_sequence);
            }
            const RollbackManifestValidationReport validation =
                ValidateRollbackSnapshotManifest(manifest, true);
            if (!validation.live_ready)
            {
                wait_or_fail("waiting-for-live-replay-manifest", 3600);
                return;
            }
            // Timeline generation parks at the terminal replay frame. That
            // park can legitimately have no live character epoch, so the
            // anchor seek must be allowed to run before requiring battle
            // pointers. Resume verification is the inverse boundary: after
            // restoring the anchor we deliberately release every lab gate,
            // and ordinary replay playback may advance or tear down that
            // frozen epoch. Only fixture-owned phases may require it.
            const bool fixture_owns_replay_epoch =
                m_status.state != RollbackReplayForkState::WaitingForAnchor
                && m_status.state != RollbackReplayForkState::VerifyingResume;
            if (fixture_owns_replay_epoch && !refresh_replay_epoch())
            {
                wait_or_fail("waiting-for-replay-battle-epoch", 3600);
                return;
            }

            switch (m_status.state)
            {
            case RollbackReplayForkState::WaitingForAnchor:
                service_anchor();
                break;
            case RollbackReplayForkState::SettlingHold:
                service_hold_settle();
                break;
            case RollbackReplayForkState::Stability:
                service_stability();
                break;
            case RollbackReplayForkState::DirectStepMatrix:
                service_direct_matrix();
                break;
            case RollbackReplayForkState::WaitingForPeer:
                service_peer();
                break;
            case RollbackReplayForkState::WaitingForGekko:
                service_gekko_start();
                break;
            case RollbackReplayForkState::Running:
                service_running();
                break;
            case RollbackReplayForkState::WaitingForFinalConsensus:
                service_final_consensus();
                break;
            case RollbackReplayForkState::VerifyingResume:
                service_resume_verification();
                break;
            default:
                break;
            }
            emit_status(false);
        }

    private:
        static bool replay_epoch_active(
            const RollbackLifecycleEpoch& epoch) noexcept
        {
            return epoch.active_battle_common()
                && epoch.presence == 10
                && !epoch.pvp_active;
        }

        bool refresh_replay_epoch() noexcept
        {
            m_epoch_failure = "replay-fork-epoch-not-run";
            ReplayScrub::ReplayForkLabContext context {};
            if (!ReplayScrub::instance().prepare_replay_fork_lab_context(
                    context)
                || !context.valid)
            {
                m_epoch_failure = context.failure;
                return false;
            }

            RollbackLifecycleEpoch next {};
            next.generation = m_lab_epoch.generation
                ? m_lab_epoch.generation : 1;
            next.battle_manager = context.battle_manager;
            next.input_log = context.input_log;
            next.chara[0] = context.chara[0];
            next.chara[1] = context.chara[1];
            next.stage_actor_manager = context.stage_actor_manager;
            next.input_log_frame = context.input_log_frame;
            next.presence = static_cast<uint8_t>(GamePresence::Replay);
            next.battle_main_state = context.battle_main_state;
            next.battle_status = context.battle_status;
            next.pvp_active = false;
            next.auto_advance_armed =
                !ReplayScrub::instance().replay_fork_lab_hold_active();

            std::array<uint8_t, 0xC0> round_start {};
            if (!SafeReadBytes(reinterpret_cast<const void*>(
                    next.battle_manager + 0x1360), round_start.data(),
                    round_start.size()))
            {
                m_epoch_failure = "replay-fork-round-start-read-failed";
                return false;
            }
            next.round_start_digest = RollbackHashRoundStartCanonical(
                round_start.data(), round_start.size());

            RollbackBreakableStageSnapshot stage {};
            const RollbackBreakableStageReport stage_report =
                CaptureRollbackBreakableStageSnapshot(
                    next.stage_actor_manager, stage);
            if (!stage_report.ok)
            {
                m_epoch_failure = stage_report.failure;
                return false;
            }
            next.stage_layout_digest = stage.stage_layout_digest;
            next.actor_set_digest = stage.actor_set_digest;
            next.valid = next.battle_manager && next.input_log
                && next.chara[0] && next.chara[1]
                && next.stage_actor_manager && next.round_start_digest
                && next.stage_layout_digest && next.actor_set_digest;

            if (m_lab_epoch.valid)
            {
                next.generation = m_lab_epoch.generation;
                if (!m_lab_epoch.same_as(next))
                {
                    ++next.generation;
                    if (!next.generation) next.generation = 1;
                }
            }
            m_lab_epoch = next;
            m_manifest.epoch = next;
            if (!replay_epoch_active(next))
            {
                m_epoch_failure = next.battle_main_state != 2
                    ? "replay-fork-battle-main-state-inactive"
                    : (next.battle_status != 2
                        ? "replay-fork-battle-status-inactive"
                        : (next.auto_advance_armed
                            ? "replay-fork-auto-advance-still-armed"
                            : "replay-fork-epoch-fields-invalid"));
                return false;
            }
            m_epoch_failure = "ok";
            return true;
        }

        bool load_input_script() noexcept
        {
            std::ifstream file(
                m_config.replay_input_file,
                std::ios::binary | std::ios::ate);
            if (!file)
            {
                m_script.failure = "replay-input-file-open-failed";
                return false;
            }
            const std::streamoff size = file.tellg();
            if (size <= 0 || size > 64ll * 1024ll * 1024ll)
            {
                m_script.failure = "replay-input-file-size-invalid";
                return false;
            }
            std::vector<uint8_t> bytes(static_cast<size_t>(size));
            file.seekg(0, std::ios::beg);
            if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
            {
                m_script.failure = "replay-input-file-read-failed";
                return false;
            }
            std::array<uint8_t, 32> digest {};
            if (!sha256(bytes, digest))
            {
                m_script.failure = "replay-sha256-compute-failed";
                return false;
            }
            if (digest != m_config.transport.replay_sha256)
            {
                m_script.failure = "replay-sha256-mismatch";
                return false;
            }
            return RollbackReplayInputScriptExtractor::extract(
                bytes, m_script);
        }

        static bool sha256(
            const std::vector<uint8_t>& bytes,
            std::array<uint8_t, 32>& digest) noexcept
        {
            digest.fill(0);
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            DWORD object_bytes = 0;
            DWORD hash_bytes = 0;
            DWORD result_bytes = 0;
            std::vector<uint8_t> object;
            bool ok = false;
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                    &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
                return false;
            if (BCRYPT_SUCCESS(BCryptGetProperty(
                    algorithm, BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&object_bytes),
                    sizeof(object_bytes), &result_bytes, 0))
                && BCRYPT_SUCCESS(BCryptGetProperty(
                    algorithm, BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hash_bytes),
                    sizeof(hash_bytes), &result_bytes, 0))
                && hash_bytes == digest.size())
            {
                try { object.resize(object_bytes); }
                catch (...) { object.clear(); }
                if (!object.empty()
                    && BCRYPT_SUCCESS(BCryptCreateHash(
                        algorithm, &hash, object.data(),
                        static_cast<ULONG>(object.size()), nullptr, 0, 0))
                    && (bytes.empty() || BCRYPT_SUCCESS(BCryptHashData(
                        hash, const_cast<PUCHAR>(bytes.data()),
                        static_cast<ULONG>(bytes.size()), 0)))
                    && BCRYPT_SUCCESS(BCryptFinishHash(
                        hash, digest.data(),
                        static_cast<ULONG>(digest.size()), 0)))
                {
                    ok = true;
                }
            }
            if (hash) BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            if (!ok) digest.fill(0);
            return ok;
        }

        void transition(
            RollbackReplayForkState state,
            const char* reason) noexcept
        {
            m_status.state = state;
            m_status.failure = reason;
            m_state_start_tick = m_status.service_ticks;
            emit_status(true);
        }

        void wait_or_fail(const char* reason, uint64_t timeout) noexcept
        {
            m_status.failure = reason;
            if (m_status.service_ticks - m_state_start_tick > timeout)
                fail(reason);
        }

        void service_anchor() noexcept
        {
            ReplayScrub& scrub = ReplayScrub::instance();
            if (!scrub.replay_fork_generate_to_anchor(
                    m_config.transport.replay_anchor_sequence))
            {
                // Native replay import/scene launch can consume well over
                // two minutes on a cold Sandboxie client. This timeout also
                // covers that pre-generation interval, so keep it above the
                // runner's replay-start budget rather than failing a healthy
                // fixture just as generation begins.
                wait_or_fail("generating-to-replay-fork-anchor", 18000);
                return;
            }
            const ReplayScrub::ReplayForkLabObservation observed =
                scrub.replay_fork_lab_observation();
            if (!observed.valid
                || observed.sequence
                    != m_config.transport.replay_anchor_sequence
                || observed.round != m_config.transport.replay_anchor_round
                || observed.master != m_config.transport.replay_anchor_master)
            {
                ReplayTraceFields fields;
                fields.string("request_id", m_config.request_id.c_str())
                    .boolean("valid", observed.valid)
                    .integer("sequence", observed.sequence)
                    .integer("round", observed.round)
                    .integer("master", observed.master)
                    .uinteger("frame_counter", observed.frame_counter)
                    .integer("expected_sequence",
                        m_config.transport.replay_anchor_sequence)
                    .integer("expected_round",
                        m_config.transport.replay_anchor_round)
                    .integer("expected_master",
                        m_config.transport.replay_anchor_master);
                ReplayDebugTrace::instance().event(
                    "rollback_replay_fork_anchor_mismatch", fields);
                wait_or_fail("replay-fork-anchor-not-landed", 1800);
                return;
            }
            ReplayScrub::ReplayForkLabContext context {};
            if (!scrub.prepare_replay_fork_lab_context(context))
            {
                wait_or_fail(context.failure, 1800);
                return;
            }
            if (!scrub.replay_fork_lab_hold_active()
                && !scrub.enter_replay_fork_lab_hold())
            {
                fail("replay-fork-hold-enter-failed");
                return;
            }
            if (!refresh_replay_epoch())
            {
                fail(m_epoch_failure);
                return;
            }
            if (!resolve_anchor_input_index(observed)) return;
            m_anchor_observation = observed;
            transition(RollbackReplayForkState::SettlingHold,
                       "settling-replay-fork-hold");
        }

        void service_hold_settle() noexcept
        {
            if (!ReplayScrub::instance().replay_fork_lab_hold_active())
            {
                fail("replay-fork-hold-lost");
                return;
            }
            if (++m_hold_settle_ticks < 3) return;
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                NativeBinding::imageBase(), m_manifest, m_anchor,
                RollbackLifecycleMode::StockOnlinePvp, true,
                nullptr, nullptr, nullptr, 0, nullptr,
                &m_palette_variant_writer_registry);
            if (!captured.ok)
            {
                fail(captured.failure);
                return;
            }
            m_anchor_captured = true;
            m_status.baseline_hash = m_anchor.canonical_hash;
            m_pair_epoch = compute_pair_epoch();
            ReplayTraceFields fields;
            append_common(fields);
            fields.hex("baseline_hash", m_status.baseline_hash)
                .hex("pair_epoch", m_pair_epoch)
                .hex("hgcpu_hash", m_anchor.hgcpu.canonical_hash)
                .uinteger("hgcpu_used_bytes", m_anchor.hgcpu.used_bytes)
                .hex("hgcpu_byte_hash", m_anchor.hgcpu.byte_hash)
                .hex("hgcpu_khit_hash",
                    m_anchor.hgcpu.khit_topology_hash)
                .hex("hgcpu_motion_bank_hash",
                    m_anchor.hgcpu.motion_bank_hash)
                .hex("hgcpu_motion_tail_hash",
                    m_anchor.hgcpu.motion_tail_hash)
                .hex("hgcpu_secondary_event_stack_hash",
                    m_anchor.hgcpu.secondary_event_stack_hash)
                .hex("hgcpu_timer_node_hash",
                    m_anchor.hgcpu.timer_node_hash)
                .hex("explicit_hash",
                    m_anchor.explicit_snapshot.canonical_hash)
                .hex("breakable_stage_hash",
                    m_anchor.breakable_stage.canonical_hash)
                .hex("stage_wind_hash",
                    m_anchor.stage_wind.canonical_hash)
                .uinteger("frame_counter", m_anchor.frame_counter)
                .hex("latest_input_p0", m_anchor.latest_input[0])
                .hex("latest_input_p1", m_anchor.latest_input[1])
                .integer("anchor_sequence",
                    m_config.transport.replay_anchor_sequence)
                .integer("anchor_round",
                    m_config.transport.replay_anchor_round)
                .integer("anchor_master",
                    m_config.transport.replay_anchor_master);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_baseline", fields);
            transition(RollbackReplayForkState::Stability,
                       "checking-frozen-fixture");
        }

        void service_stability() noexcept
        {
            const ReplayScrub::ReplayForkLabObservation observed =
                ReplayScrub::instance().replay_fork_lab_observation();
            if (!observed.valid
                || observed.sequence != m_anchor_observation.sequence
                || observed.round != m_anchor_observation.round
                || observed.master != m_anchor_observation.master
                || observed.frame_counter
                    != m_anchor_observation.frame_counter
                || !ValidateRollbackReplayForkLifecycleEpoch(
                    m_anchor.explicit_snapshot.epoch, m_manifest.epoch).ok)
            {
                fail("replay-fork-uncredited-drift");
                return;
            }
            ++m_stability_observations;
            if (m_stability_observations < m_config.stability_ticks) return;

            RollbackStepState verification {};
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                NativeBinding::imageBase(), m_manifest, verification,
                RollbackLifecycleMode::StockOnlinePvp, true,
                nullptr, nullptr, nullptr, 0, nullptr,
                &m_palette_variant_writer_registry);
            if (!captured.ok
                || verification.canonical_hash != m_status.baseline_hash)
            {
                fail(captured.ok
                    ? "replay-fork-hold-hash-drift" : captured.failure);
                return;
            }
            m_status.hold_stable = true;
            ReplayTraceFields fields;
            append_common(fields);
            fields.uinteger("uncredited_ticks", m_stability_observations)
                .hex("canonical_hash", verification.canonical_hash)
                .boolean("stable", true);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_stability", fields);
            transition(RollbackReplayForkState::DirectStepMatrix,
                       "running-direct-step-matrix");
        }

        bool resolve_anchor_input_index(
            const ReplayScrub::ReplayForkLabObservation& observed) noexcept
        {
            if (observed.round < 0
                || static_cast<size_t>(observed.round)
                    >= m_script.round_pairs.size())
            {
                fail("replay-fork-anchor-round-input-pair-missing");
                return false;
            }
            size_t prefix = 0;
            for (int32_t round = 0; round < observed.round; ++round)
                prefix += m_script.round_pairs[
                    static_cast<size_t>(round)].frame_count;
            const size_t index = prefix
                + static_cast<size_t>(observed.master);
            const size_t available = (std::min)(
                m_script.player_inputs[0].size(),
                m_script.player_inputs[1].size());
            if (index >= available)
            {
                fail("replay-fork-anchor-input-index-out-of-range");
                return false;
            }
            const uintptr_t base = NativeBinding::imageBase();
            uint64_t live_inputs[2] {};
            if (!base || !SafeReadBytes(reinterpret_cast<const void*>(
                    base + 0x4855700), live_inputs, sizeof(live_inputs)))
            {
                fail("replay-fork-anchor-live-input-unreadable");
                return false;
            }
            if (static_cast<uint32_t>(live_inputs[0])
                    != m_script.player_inputs[0][index]
                || static_cast<uint32_t>(live_inputs[1])
                    != m_script.player_inputs[1][index])
            {
                fail("replay-fork-anchor-input-mapping-mismatch");
                return false;
            }
            m_anchor_input_index = index;
            ReplayTraceFields fields;
            append_common(fields);
            fields.uinteger("input_index", index)
                .integer("round", observed.round)
                .integer("master", observed.master)
                .hex("player0_input", live_inputs[0])
                .hex("player1_input", live_inputs[1]);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_input_anchor", fields);
            return true;
        }

        void service_direct_matrix() noexcept
        {
            static constexpr uint32_t kWindows[] = {1, 2, 8, 15, 60};
            // Bring up the authenticated transport before the expensive
            // direct matrix. The worker owns heartbeats independently, so a
            // faster client can wait for the later Sandboxie fixture without
            // consuming the short outer-tick readiness budget afterward.
            if (!m_network_started)
            {
                if (!m_network.start(m_config.transport))
                {
                    fail("replay-fork-udp-start-failed");
                    return;
                }
                m_network_started = true;
                ReplayTraceFields fields;
                append_common(fields);
                fields.string("bind_address",
                        m_config.transport.bind_address)
                    .uinteger("bind_port", m_config.transport.bind_port)
                    .string("peer_address",
                        m_config.transport.peer_address)
                    .uinteger("peer_port", m_config.transport.peer_port)
                    .uinteger("local_peer", m_config.transport.local_peer)
                    .uinteger("remote_peer", m_config.transport.remote_peer)
                    .string("network_profile",
                        RollbackNetworkProfileName(
                            m_config.transport.network_profile));
                ReplayDebugTrace::instance().event(
                    "rollback_replay_fork_udp_started", fields);
            }
            if (m_direct_window_index >= std::size(kWindows))
            {
                if (!restore_anchor("direct-matrix-complete")) return;
                transition(RollbackReplayForkState::WaitingForPeer,
                           "waiting-for-authenticated-peer");
                return;
            }
            if (!restore_anchor("direct-window-start")) return;
            const uint32_t window = kWindows[m_direct_window_index];
            const size_t available = (std::min)(
                m_script.player_inputs[0].size(),
                m_script.player_inputs[1].size());
            if (available == 0
                || m_anchor_input_index + window > available)
            {
                fail("replay-fork-input-script-empty");
                return;
            }
            const ReplayScrub::ReplayForkLabObservation before =
                ReplayScrub::instance().replay_fork_lab_observation();
            for (uint32_t offset = 0; offset < window; ++offset)
            {
                const size_t index = m_anchor_input_index + offset;
                if (!ReplayScrub::instance().replay_fork_direct_advance(
                        m_script.player_inputs[0][index],
                        m_script.player_inputs[1][index]))
                {
                    fail("replay-fork-direct-advance-failed");
                    return;
                }
            }
            RollbackStepState state {};
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                NativeBinding::imageBase(), m_manifest, state,
                RollbackLifecycleMode::StockOnlinePvp, true,
                nullptr, nullptr, nullptr, 0, nullptr,
                &m_palette_variant_writer_registry);
            if (!captured.ok)
            {
                fail(captured.failure);
                return;
            }
            const ReplayScrub::ReplayForkLabObservation after =
                ReplayScrub::instance().replay_fork_lab_observation();
            ReplayTraceFields fields;
            append_common(fields);
            fields.uinteger("window", window)
                .hex("post_hash", state.canonical_hash)
                .uinteger("frame_before", before.frame_counter)
                .uinteger("frame_after", after.frame_counter)
                .uinteger("frame_delta",
                    after.frame_counter - before.frame_counter)
                .uinteger("matrix_index", m_direct_window_index);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_direct_step", fields);
            if (!present_current_state(
                    "direct-step-matrix", window,
                    state.canonical_hash))
            {
                fail("replay-fork-presentation-sync-failed");
                return;
            }
            ++m_direct_window_index;
            m_status.completed_direct_windows =
                static_cast<uint32_t>(m_direct_window_index);
        }

        void service_peer() noexcept
        {
            if (!m_network_started)
            {
                if (!m_network.start(m_config.transport))
                {
                    fail("replay-fork-udp-start-failed");
                    return;
                }
                m_network_started = true;
            }
            const RollbackUdpWorkerStatus network = m_network.status();
            if (!network.peer_ready
                || network.failure != RollbackUdpWorkerFailure::None)
            {
                wait_or_fail("waiting-for-authenticated-peer", 1800);
                return;
            }
            m_handshake_generation = network.handshake_generation;
            if (!exchange_baseline_proof()) return;
            RollbackGekkoRuntimeConfig config {};
            config.local_player_slot =
                m_config.transport.local_player_slot;
            config.remote_peer = m_config.transport.remote_peer;
            config.rollback_window = m_config.transport.rollback_window;
            config.input_delay = m_config.transport.input_delay;
            config.state_size = sizeof(RollbackSnapshotHandle);
            RollbackGekkoRuntimeCallbacks callbacks {};
            callbacks.context = this;
            callbacks.send = &core_send;
            callbacks.receive = &core_receive;
            callbacks.game_event = &core_game_event;
            callbacks.idle_update = &core_idle_update;
            callbacks.failure = &core_failure;
            if (!m_gekko.start(config, callbacks))
            {
                fail("replay-fork-gekko-start-failed");
                return;
            }
            m_run_input_index = m_anchor_input_index;
            transition(RollbackReplayForkState::WaitingForGekko,
                       "waiting-for-gekko-session");
        }

        void service_gekko_start() noexcept
        {
            if (!network_valid())
            {
                fail("replay-fork-peer-readiness-lost");
                return;
            }
            if (!m_gekko.poll())
            {
                if (m_core_failure_reason)
                    fail(m_core_failure_reason);
                return;
            }
            const size_t slot = m_config.transport.local_player_slot;
            const std::vector<uint32_t>& local =
                m_script.player_inputs[slot];
            if (local.empty())
            {
                fail("replay-fork-local-input-empty");
                return;
            }
            if (m_anchor_input_index >= local.size())
            {
                fail("replay-fork-input-script-exhausted");
                return;
            }
            // Gekko can require many update calls before both actors report
            // GekkoSessionStarted, especially through the impaired transport.
            // Those connection-bootstrap calls do not own successive game
            // frames. Reuse the anchor input until the start event arrives;
            // otherwise network latency shifts the replay input stream and an
            // impaired run can never match the clean oracle.
            const uint32_t input = local[m_anchor_input_index];
            record_owned_input(input);
            ++m_gekko_bootstrap_submissions;
            if (!m_gekko.update(input, nullptr))
            {
                fail(m_core_failure_reason
                    ? m_core_failure_reason
                    : "replay-fork-gekko-bootstrap-failed");
                return;
            }
            if (m_current_frame.valid
                && !present_current_state(
                    "gekko-bootstrap", m_current_frame.value,
                    m_last_advance_canonical_hash))
            {
                fail("replay-fork-presentation-sync-failed");
                return;
            }
            if (!m_gekko.session_started()) return;
            m_run_input_index = m_anchor_input_index + 1;
            ReplayTraceFields fields;
            append_common(fields);
            fields.uinteger("bootstrap_submissions",
                    m_gekko_bootstrap_submissions)
                .uinteger("anchor_input_index", m_anchor_input_index)
                .uinteger("next_input_index", m_run_input_index)
                .hex("bootstrap_input", input);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_gekko_started", fields);
            m_status.gekko_started = true;
            transition(RollbackReplayForkState::Running,
                       "running-gekko-replay-fork");
        }

        void service_running() noexcept
        {
            if (!network_valid())
            {
                fail("replay-fork-peer-readiness-lost");
                return;
            }
            const size_t slot = m_config.transport.local_player_slot;
            const std::vector<uint32_t>& local =
                m_script.player_inputs[slot];
            if (local.empty())
            {
                fail("replay-fork-local-input-empty");
                return;
            }
            if (m_run_input_index >= local.size())
            {
                fail("replay-fork-input-script-exhausted");
                return;
            }
            const uint32_t input = local[m_run_input_index];
            ++m_run_input_index;
            record_owned_input(input);
            if (!m_gekko.update(input, nullptr))
            {
                fail(m_core_failure_reason
                    ? m_core_failure_reason
                    : "replay-fork-gekko-update-failed");
                return;
            }
            if (m_current_frame.valid
                && !present_current_state(
                    "gekko-update", m_current_frame.value,
                    m_last_advance_canonical_hash))
            {
                fail("replay-fork-presentation-sync-failed");
                return;
            }
            if (!network_valid())
            {
                fail("replay-fork-peer-generation-changed");
                return;
            }
            if (m_status.completed_frames >= m_config.run_frames)
                transition(
                    RollbackReplayForkState::WaitingForFinalConsensus,
                    "waiting-for-final-consensus");
        }

        void service_final_consensus() noexcept
        {
            if (!network_valid())
            {
                fail("replay-fork-peer-readiness-lost-at-final-consensus");
                return;
            }
            if (!m_gekko.poll())
            {
                fail(m_core_failure_reason
                    ? m_core_failure_reason
                    : "replay-fork-final-consensus-poll-failed");
                return;
            }
            if (m_core_failure_reason)
            {
                fail(m_core_failure_reason);
                return;
            }
            if (m_terminal_summary_valid
                && !m_network.enqueue(
                    RollbackProtocolV2PacketType::Input,
                    &m_terminal_summary, sizeof(m_terminal_summary), {},
                    m_handshake_generation))
            {
                fail("replay-fork-terminal-summary-resend-failed");
                return;
            }
            if (!m_final_summary_accepted
                && m_terminal_summary_conflict)
            {
                if (!m_gekko.flush_terminal_corrections(nullptr))
                {
                    fail(m_core_failure_reason
                        ? m_core_failure_reason
                        : "replay-fork-final-consensus-correction-flush-failed");
                    return;
                }
            }
            if (!m_final_summary_accepted)
            {
                wait_or_fail("waiting-for-terminal-frame-consensus", 1800);
                return;
            }
            if (!finalize_terminal_evidence())
            {
                fail(m_core_failure_reason
                    ? m_core_failure_reason
                    : "replay-fork-terminal-evidence-failed");
                return;
            }
            if (!exchange_terminal_barrier()) return;
            if (m_status.pair_accepts == 0)
            {
                wait_or_fail("waiting-for-replay-fork-consensus", 1800);
                return;
            }
            if (m_config.require_rollback
                && (m_status.loads == 0
                    || m_status.rollback_advances == 0))
            {
                wait_or_fail("waiting-for-forced-rollback", 3600);
                return;
            }
            if (m_status.presentation_syncs < 5
                || m_status.presentation_failures != 0
                || !m_status.presentation_gameplay_unchanged
                || !m_status.presentation_motion_observed)
            {
                fail("replay-fork-presentation-evidence-incomplete");
                return;
            }
            begin_resume_verification();
        }

        bool finalize_terminal_evidence() noexcept
        {
            if (!m_terminal_evidence_dirty
                && m_status.evidence_frames == m_config.run_frames
                && m_status.terminal_evidence_hash != 0)
                return true;
            if (m_local_summaries.size() < m_config.run_frames)
            {
                m_core_failure_reason =
                    "replay-fork-terminal-evidence-table-short";
                return false;
            }
            RollbackHash evidence {};
            uint64_t local_input_hash = 0;
            uint64_t remote_input_hash = 0;
            const uint8_t local_slot =
                m_config.transport.local_player_slot;
            for (uint32_t frame = 0; frame < m_config.run_frames; ++frame)
            {
                const RollbackReplayForkFrameSummary& summary =
                    m_local_summaries[frame];
                if (summary.frame != frame || summary.canonical_hash == 0)
                {
                    m_core_failure_reason =
                        "replay-fork-terminal-evidence-frame-missing";
                    return false;
                }
                evidence.add_scalar(frame);
                evidence.add_scalar(summary.canonical_hash);
                evidence.add_scalar(summary.input[0]);
                evidence.add_scalar(summary.input[1]);
                const uint32_t local = summary.input[local_slot];
                const uint32_t remote = summary.input[local_slot ^ 1u];
                local_input_hash += hash_input_sample(frame, local);
                remote_input_hash += hash_input_sample(frame, remote);
            }
            m_status.local_input_hash = local_input_hash;
            m_status.remote_input_hash = remote_input_hash;
            m_status.evidence_frames = m_config.run_frames;
            m_status.terminal_evidence_hash = evidence.value;
            m_status.summary_overwrites = 0;
            m_terminal_evidence_dirty = false;
            return evidence.value != 0;
        }

        bool send_terminal_barrier(uint32_t phase) noexcept
        {
            if (!m_final_frame.valid || !m_final_summary_accepted)
                return false;
            RollbackReplayForkTerminalProof proof {};
            proof.phase = phase;
            proof.frame = m_final_frame.value;
            proof.owned_input_count = m_status.evidence_frames;
            proof.pair_epoch = m_pair_epoch;
            proof.canonical_hash = m_status.final_hash;
            proof.owned_input_hash = m_status.terminal_evidence_hash;
            proof.owned_player_slot =
                m_config.transport.local_player_slot;
            return m_network.enqueue(
                RollbackProtocolV2PacketType::LaunchBarrier,
                &proof, sizeof(proof), {}, m_handshake_generation);
        }

        void accept_terminal_barrier(
            const RollbackUdpMessage& message) noexcept
        {
            // A late baseline proof shares LaunchBarrier but has a distinct
            // size and magic; it is safe to ignore after Gekko has started.
            if (message.payload_bytes
                != sizeof(RollbackReplayForkTerminalProof))
                return;
            RollbackReplayForkTerminalProof peer {};
            std::memcpy(&peer, message.payload.data(), sizeof(peer));
            if (peer.magic != RollbackReplayForkTerminalProof{}.magic)
                return;
            if (!m_final_frame.valid || !m_final_summary_accepted)
                return;
            // The peer can finish its final-summary match and send phase 1
            // while this process is still inside m_gekko.poll(), before
            // finalize_terminal_evidence() has run below that poll. Defer the
            // proof instead of comparing it against an uninitialized local
            // evidence hash. Phase 1 is retransmitted every 15 service ticks.
            if (m_status.evidence_frames != m_config.run_frames
                || m_status.terminal_evidence_hash == 0
                || m_terminal_evidence_dirty)
                return;
            if ((peer.phase != 1 && peer.phase != 2)
                || peer.frame != m_final_frame.value
                || peer.owned_input_count != m_status.evidence_frames
                || peer.pair_epoch != m_pair_epoch
                || peer.canonical_hash != m_status.final_hash
                || peer.owned_input_hash
                    != m_status.terminal_evidence_hash
                || peer.owned_player_slot
                    != (m_config.transport.local_player_slot ^ 1u))
            {
                m_core_failure_reason =
                    "replay-fork-terminal-barrier-mismatch";
                return;
            }
            if (peer.phase == 1)
            {
                m_status.peer_final_hash = peer.canonical_hash;
                m_terminal_proof_accepted = true;
                if (!send_terminal_barrier(2))
                    m_core_failure_reason =
                        "replay-fork-terminal-ack-send-failed";
            }
            else
            {
                m_status.peer_final_hash = peer.canonical_hash;
                m_terminal_ack_accepted = true;
            }
        }

        bool exchange_terminal_barrier() noexcept
        {
            if (!m_terminal_proof_sent
                || (m_status.service_ticks % 15u) == 0u)
            {
                if (!send_terminal_barrier(1))
                {
                    fail("replay-fork-terminal-proof-send-failed");
                    return false;
                }
                m_terminal_proof_sent = true;
            }
            if (m_terminal_proof_accepted
                && (m_status.service_ticks % 15u) == 0u
                && !send_terminal_barrier(2))
            {
                fail("replay-fork-terminal-ack-send-failed");
                return false;
            }
            if (!m_terminal_proof_accepted || !m_terminal_ack_accepted)
            {
                wait_or_fail("waiting-for-terminal-proof-barrier", 1800);
                return false;
            }
            if (m_terminal_barrier_complete_tick == 0)
                m_terminal_barrier_complete_tick = m_status.service_ticks;
            // Continue acknowledgements briefly so the peer cannot be left
            // waiting when this process releases the fixture first.
            if (m_status.service_ticks
                    - m_terminal_barrier_complete_tick < 30u)
            {
                if (!send_terminal_barrier(2))
                {
                    fail("replay-fork-terminal-ack-settle-failed");
                    return false;
                }
                wait_or_fail("settling-terminal-proof-barrier", 1800);
                return false;
            }
            return true;
        }

        bool restore_anchor(const char* reason) noexcept
        {
            if (!m_anchor_captured) return false;
            const RollbackStepStateReport restored = RestoreRollbackStepState(
                NativeBinding::imageBase(), m_anchor, true,
                RollbackLifecycleMode::StockOnlinePvp, true, true,
                nullptr, nullptr, nullptr,
                &m_palette_variant_writer_registry);
            if (!restored.ok)
            {
                ReplayTraceFields fields;
                append_common(fields);
                fields.string("reason", reason ? reason : "?")
                    .string("failure", restored.failure)
                    .hex("expected_canonical_hash",
                        restored.expected_canonical_hash)
                    .hex("verification_canonical_hash",
                        restored.verification_canonical_hash)
                    .hex("expected_explicit_hash",
                        restored.expected_explicit_hash)
                    .hex("verification_explicit_hash",
                        restored.verification_explicit_hash)
                    .hex("expected_stage_hash",
                        restored.expected_stage_hash)
                    .hex("verification_stage_hash",
                        restored.verification_stage_hash)
                    .hex("expected_hgcpu_hash",
                        restored.verification_hgcpu_compare.hash_a)
                    .hex("verification_hgcpu_hash",
                        restored.verification_hgcpu_compare.hash_b)
                    .hex("expected_motion_bank_hash",
                        restored.verification_hgcpu_compare
                            .motion_bank_hash_a)
                    .hex("verification_motion_bank_hash",
                        restored.verification_hgcpu_compare
                            .motion_bank_hash_b)
                    .hex("expected_motion_tail_hash",
                        restored.verification_hgcpu_compare
                            .motion_tail_hash_a)
                    .hex("verification_motion_tail_hash",
                        restored.verification_hgcpu_compare
                            .motion_tail_hash_b)
                    .hex("expected_timer_node_hash",
                        restored.verification_hgcpu_compare
                            .timer_node_hash_a)
                    .hex("verification_timer_node_hash",
                        restored.verification_hgcpu_compare
                            .timer_node_hash_b)
                    .uinteger("hgcpu_unignored_mismatches",
                        restored.verification_hgcpu_compare
                            .unignored_mismatch_count)
                    .uinteger("hgcpu_first_unignored_offset",
                        restored.verification_hgcpu_compare
                            .first_unignored_mismatch_offset)
                    .uinteger("hgcpu_first_unignored_local",
                        restored.verification_hgcpu_compare
                            .first_unignored_dynamic_local)
                    .uinteger("hgcpu_first_unignored_player",
                        restored.verification_hgcpu_compare
                            .first_unignored_dynamic_player)
                    .boolean("hgcpu_policy_match",
                        restored.verification_hgcpu_compare.policy_match);
                ReplayDebugTrace::instance().event(
                    "rollback_replay_fork_restore_failure", fields);
                fail(restored.failure);
                return false;
            }
            RollbackStepState verified {};
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                NativeBinding::imageBase(), m_manifest, verified,
                RollbackLifecycleMode::StockOnlinePvp, true,
                nullptr, nullptr, nullptr, 0, nullptr,
                &m_palette_variant_writer_registry);
            if (!captured.ok
                || verified.canonical_hash != m_anchor.canonical_hash)
            {
                fail(captured.ok
                    ? "replay-fork-anchor-restore-mismatch"
                    : captured.failure);
                return false;
            }
            m_status.baseline_restored = true;
            (void)reason;
            return true;
        }

        struct ReplayForkPresentationPositionProbe
        {
            float simulation[3] {};
            float render[3] {};
        };

        static bool read_presentation_position_probe(
            uintptr_t chara,
            ReplayForkPresentationPositionProbe& out) noexcept
        {
            return chara
                && SafeReadBytes(reinterpret_cast<const void*>(chara + 0xA0),
                                 out.simulation, sizeof(out.simulation))
                && SafeReadBytes(reinterpret_cast<const void*>(chara + 0x2090),
                                 out.render, sizeof(out.render));
        }

        static bool same_lab_observation(
            const ReplayScrub::ReplayForkLabObservation& a,
            const ReplayScrub::ReplayForkLabObservation& b) noexcept
        {
            return a.valid && b.valid
                && a.sequence == b.sequence
                && a.round == b.round
                && a.master == b.master
                && a.frame_counter == b.frame_counter;
        }

        static bool finite_transform(
            const NativeFTransform48& transform) noexcept
        {
            const float* values = transform.rotation;
            for (size_t index = 0; index < 12; ++index)
                if (!std::isfinite(values[index])) return false;
            return true;
        }

        bool resolve_presentation_actors() noexcept
        {
            if (m_presentation_actor[0] && m_presentation_actor[1])
                return true;

            const TArrHdr* property = m_lux.battleCharaArray();
            TArrHdr actors {};
            if (!property
                || !SafeReadBytes(property, &actors, sizeof(actors))
                || !actors.Data || actors.Num < 2 || actors.Num > 16
                || actors.Max < actors.Num || actors.Max > 64)
                return false;

            void* resolved[2] {};
            int32_t array_index[2] {-1, -1};
            for (int32_t index = 0; index < actors.Num; ++index)
            {
                void* actor = nullptr;
                int32_t player_index = -1;
                const auto* slot = reinterpret_cast<const uint8_t*>(
                    actors.Data) + sizeof(void*) * index;
                if (!SafeReadPtr(slot, &actor) || !actor
                    || !SafeReadInt32(
                        reinterpret_cast<const uint8_t*>(actor) + 0x3A0,
                        &player_index)
                    || player_index < 0 || player_index > 1)
                    continue;
                if (resolved[player_index]
                    && resolved[player_index] != actor)
                    return false;
                resolved[player_index] = actor;
                array_index[player_index] = index;
            }
            if (!resolved[0] || !resolved[1]
                || resolved[0] == resolved[1])
                return false;

            // Actor identity is process-local and must remain fixed for the
            // frozen fixture epoch. It is intentionally absent from pair
            // epoch and canonical cross-peer hashing.
            m_presentation_actor[0] = resolved[0];
            m_presentation_actor[1] = resolved[1];
            m_presentation_actor_array_index[0] = array_index[0];
            m_presentation_actor_array_index[1] = array_index[1];
            m_presentation_actor_array_count = actors.Num;
            return true;
        }

        bool read_actor_location(void* actor, FVec3& out) noexcept
        {
            out = {};
            if (!actor) return false;
            Obj wrapped {reinterpret_cast<RC::Unreal::UObject*>(actor)};
            out = wrapped.callVec3(
                m_actor_location_fn, L"K2_GetActorLocation");
            return m_actor_location_fn.raw()
                && std::isfinite(out.X) && std::isfinite(out.Y)
                && std::isfinite(out.Z);
        }

        static bool actor_location_matches_transform(
            const FVec3& location,
            const NativeFTransform48& transform) noexcept
        {
            constexpr float kReadbackTolerance = 0.01f;
            return std::fabs(location.X - transform.translation[0])
                    <= kReadbackTolerance
                && std::fabs(location.Y - transform.translation[1])
                    <= kReadbackTolerance
                && std::fabs(location.Z - transform.translation[2])
                    <= kReadbackTolerance;
        }

        bool present_current_state(
            const char* reason,
            uint64_t presentation_credit,
            uint64_t expected_canonical_hash) noexcept
        {
            if (!ReplayScrub::instance().replay_fork_lab_hold_active()
                || !m_lab_epoch.valid || !m_lab_epoch.chara[0]
                || !m_lab_epoch.chara[1] || !expected_canonical_hash
                || !resolve_presentation_actors())
            {
                ++m_status.presentation_failures;
                m_status.presentation_gameplay_unchanged = false;
                return false;
            }
            if (m_last_presentation_hash == expected_canonical_hash)
            {
                ++m_status.presentation_skips;
                return true;
            }

            const ReplayScrub::ReplayForkLabObservation observation_before =
                ReplayScrub::instance().replay_fork_lab_observation();
            ReplayForkPresentationPositionProbe position_before[2] {};
            ReplayForkPresentationPositionProbe position_after[2] {};
            NativeFTransform48 transform[2] {};
            NativePresentationPublishReport publish_report[2] {};
            FVec3 actor_location_before[2] {};
            FVec3 actor_location_after[2] {};
            bool published[2] {};
            bool probes_read = true;
            for (size_t player = 0; player < 2; ++player)
            {
                if (!read_presentation_position_probe(
                        m_lab_epoch.chara[player], position_before[player])
                    || !read_actor_location(
                        m_presentation_actor[player],
                        actor_location_before[player]))
                    probes_read = false;
            }
            for (size_t player = 0; probes_read && player < 2; ++player)
            {
                published[player] =
                    NativeBinding::publishCharaPresentationTransform(
                        m_presentation_actor[player], transform[player],
                        &publish_report[player]);
                if (!published[player]
                    || !finite_transform(transform[player]))
                    break;
            }
            for (size_t player = 0; player < 2; ++player)
            {
                if (!read_presentation_position_probe(
                        m_lab_epoch.chara[player], position_after[player])
                    || !read_actor_location(
                        m_presentation_actor[player],
                        actor_location_after[player]))
                    probes_read = false;
            }

            RollbackStepState verification {};
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                NativeBinding::imageBase(), m_manifest, verification,
                RollbackLifecycleMode::StockOnlinePvp, true,
                nullptr, nullptr, nullptr, 0, nullptr,
                &m_palette_variant_writer_registry);
            const ReplayScrub::ReplayForkLabObservation observation_after =
                ReplayScrub::instance().replay_fork_lab_observation();
            const bool position_fields_unchanged =
                std::memcmp(position_before, position_after,
                            sizeof(position_before)) == 0;
            const bool actor_readback_matches = probes_read
                && actor_location_matches_transform(
                    actor_location_after[0], transform[0])
                && actor_location_matches_transform(
                    actor_location_after[1], transform[1]);
            const bool gameplay_unchanged = probes_read
                && published[0] && published[1]
                && captured.ok
                && verification.canonical_hash == expected_canonical_hash
                && same_lab_observation(
                    observation_before, observation_after)
                && position_fields_unchanged
                && actor_readback_matches;

            ReplayTraceFields fields;
            append_common(fields);
            fields.string("reason", reason ? reason : "?")
                .uinteger("presentation_credit", presentation_credit)
                .hex("expected_canonical_hash", expected_canonical_hash)
                .hex("verification_canonical_hash",
                    verification.canonical_hash)
                .boolean("capture_ok", captured.ok)
                .boolean("player0_published", published[0])
                .boolean("player1_published", published[1])
                .boolean("probes_read", probes_read)
                .boolean("actor_readback_matches", actor_readback_matches)
                .boolean("position_fields_unchanged",
                    position_fields_unchanged)
                .boolean("observation_unchanged",
                    same_lab_observation(
                        observation_before, observation_after))
                .boolean("gameplay_unchanged", gameplay_unchanged)
                .uinteger("actor_array_count",
                    m_presentation_actor_array_count)
                .hex("player0_simulation_chara", m_lab_epoch.chara[0])
                .hex("player0_actor", reinterpret_cast<uintptr_t>(
                    m_presentation_actor[0]))
                .integer("player0_actor_array_index",
                    m_presentation_actor_array_index[0])
                .hex("player0_actor_vtable", publish_report[0].vtable)
                .hex("player0_transform_getter",
                    publish_report[0].transform_getter)
                .hex("player0_root_component",
                    publish_report[0].root_component)
                .boolean("player0_getter_called",
                    publish_report[0].getter_called)
                .boolean("player0_setter_called",
                    publish_report[0].setter_called)
                .boolean("player0_setter_result",
                    publish_report[0].setter_returned_true)
                .boolean("player0_publish_exception",
                    publish_report[0].exception)
                .real("player0_sim_x", position_before[0].simulation[0])
                .real("player0_sim_y", position_before[0].simulation[1])
                .real("player0_sim_z", position_before[0].simulation[2])
                .real("player0_render_x", position_before[0].render[0])
                .real("player0_render_y", position_before[0].render[1])
                .real("player0_render_z", position_before[0].render[2])
                .real("player0_actor_x", transform[0].translation[0])
                .real("player0_actor_y", transform[0].translation[1])
                .real("player0_actor_z", transform[0].translation[2])
                .real("player0_actor_before_x",
                    actor_location_before[0].X)
                .real("player0_actor_before_y",
                    actor_location_before[0].Y)
                .real("player0_actor_before_z",
                    actor_location_before[0].Z)
                .real("player0_actor_readback_x",
                    actor_location_after[0].X)
                .real("player0_actor_readback_y",
                    actor_location_after[0].Y)
                .real("player0_actor_readback_z",
                    actor_location_after[0].Z)
                .hex("player1_simulation_chara", m_lab_epoch.chara[1])
                .hex("player1_actor", reinterpret_cast<uintptr_t>(
                    m_presentation_actor[1]))
                .integer("player1_actor_array_index",
                    m_presentation_actor_array_index[1])
                .hex("player1_actor_vtable", publish_report[1].vtable)
                .hex("player1_transform_getter",
                    publish_report[1].transform_getter)
                .hex("player1_root_component",
                    publish_report[1].root_component)
                .boolean("player1_getter_called",
                    publish_report[1].getter_called)
                .boolean("player1_setter_called",
                    publish_report[1].setter_called)
                .boolean("player1_setter_result",
                    publish_report[1].setter_returned_true)
                .boolean("player1_publish_exception",
                    publish_report[1].exception)
                .real("player1_sim_x", position_before[1].simulation[0])
                .real("player1_sim_y", position_before[1].simulation[1])
                .real("player1_sim_z", position_before[1].simulation[2])
                .real("player1_render_x", position_before[1].render[0])
                .real("player1_render_y", position_before[1].render[1])
                .real("player1_render_z", position_before[1].render[2])
                .real("player1_actor_x", transform[1].translation[0])
                .real("player1_actor_y", transform[1].translation[1])
                .real("player1_actor_z", transform[1].translation[2])
                .real("player1_actor_before_x",
                    actor_location_before[1].X)
                .real("player1_actor_before_y",
                    actor_location_before[1].Y)
                .real("player1_actor_before_z",
                    actor_location_before[1].Z);
            fields.real("player1_actor_readback_x",
                    actor_location_after[1].X)
                .real("player1_actor_readback_y",
                    actor_location_after[1].Y)
                .real("player1_actor_readback_z",
                    actor_location_after[1].Z);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_presentation_sync", fields);

            if (!gameplay_unchanged)
            {
                ++m_status.presentation_failures;
                m_status.presentation_gameplay_unchanged = false;
                return false;
            }

            if (m_last_presentation_valid)
            {
                for (size_t player = 0; player < 2; ++player)
                {
                    if (std::memcmp(
                            m_last_published_transform[player].translation,
                            transform[player].translation,
                            sizeof(transform[player].translation)) != 0)
                    {
                        m_status.presentation_motion_observed = true;
                        break;
                    }
                }
            }
            std::memcpy(m_last_published_transform, transform,
                        sizeof(transform));
            m_last_presentation_valid = true;
            m_last_presentation_hash = expected_canonical_hash;
            ++m_status.presentation_syncs;
            return true;
        }

        uint64_t compute_pair_epoch() const noexcept
        {
            RollbackHash hash;
            hash.add_scalar(m_config.transport.expected_build_id);
            hash.add_scalar(m_config.transport.expected_schema_id);
            hash.add_bytes(m_config.transport.replay_sha256.data(),
                           m_config.transport.replay_sha256.size());
            hash.add_scalar(m_config.transport.replay_anchor_sequence);
            hash.add_scalar(m_config.transport.replay_anchor_round);
            hash.add_scalar(m_config.transport.replay_anchor_master);
            hash.add_scalar(m_config.transport.replay_run_nonce_hash);
            hash.add_scalar(m_manifest.epoch.round_start_digest);
            hash.add_scalar(m_manifest.epoch.stage_layout_digest);
            hash.add_scalar(m_status.baseline_hash);
            // actor_set_digest includes process-local actor pointers and is
            // used only for local restore identity. Never put it in the
            // cross-peer replay epoch.
            return hash.value ? hash.value : 1;
        }

        bool exchange_baseline_proof() noexcept
        {
            auto send_barrier = [&](uint32_t phase) noexcept -> bool
            {
                RollbackReplayForkBaselineProof proof {};
                proof.phase = phase;
                proof.anchor_sequence =
                    m_config.transport.replay_anchor_sequence;
                proof.anchor_round = m_config.transport.replay_anchor_round;
                proof.anchor_master =
                    m_config.transport.replay_anchor_master;
                proof.pair_epoch = m_pair_epoch;
                proof.baseline_hash = m_status.baseline_hash;
                if (!m_network.enqueue(
                        RollbackProtocolV2PacketType::LaunchBarrier,
                        &proof, sizeof(proof), {},
                        m_handshake_generation))
                {
                    fail("replay-fork-baseline-proof-send-failed");
                    return false;
                }
                return true;
            };
            if (!m_baseline_proof_sent
                || (m_status.service_ticks % 15u) == 0u)
            {
                if (!send_barrier(1)) return false;
                m_baseline_proof_sent = true;
            }
            if (m_baseline_proof_accepted
                && (m_status.service_ticks % 15u) == 0u)
            {
                if (!send_barrier(2)) return false;
            }
            RollbackUdpMessage message {};
            while (m_network.dequeue(message))
            {
                if (message.handshake_generation != m_handshake_generation)
                {
                    fail("replay-fork-baseline-proof-generation-mismatch");
                    return false;
                }
                if (message.packet_type
                        != RollbackProtocolV2PacketType::LaunchBarrier
                    || message.payload_bytes != sizeof(
                        RollbackReplayForkBaselineProof))
                    continue;
                RollbackReplayForkBaselineProof peer {};
                std::memcpy(&peer, message.payload.data(), sizeof(peer));
                if (peer.magic != RollbackReplayForkBaselineProof{}.magic
                    || (peer.phase != 1 && peer.phase != 2)
                    || peer.anchor_sequence
                        != m_config.transport.replay_anchor_sequence
                    || peer.anchor_round
                        != m_config.transport.replay_anchor_round
                    || peer.anchor_master
                        != m_config.transport.replay_anchor_master
                    || peer.pair_epoch != m_pair_epoch
                    || peer.baseline_hash != m_status.baseline_hash)
                {
                    fail("replay-fork-baseline-proof-mismatch");
                    return false;
                }
                if (peer.phase == 1)
                {
                    m_baseline_proof_accepted = true;
                    // Acknowledge immediately as well as periodically so the
                    // peer cannot leave this barrier after a one-shot packet.
                    if (!send_barrier(2)) return false;
                }
                else
                {
                    m_baseline_ack_accepted = true;
                }
            }
            if (!m_baseline_proof_accepted || !m_baseline_ack_accepted)
            {
                wait_or_fail("waiting-for-baseline-proof", 1800);
                return false;
            }
            if (m_baseline_barrier_complete_tick == 0)
                m_baseline_barrier_complete_tick = m_status.service_ticks;
            // Briefly continue proof/ack broadcasts on both peers. This
            // bounds transition skew without relying on launch timing, and
            // Gekko's own connection phase starts only after the barrier.
            if (m_status.service_ticks
                    - m_baseline_barrier_complete_tick < 30u)
            {
                wait_or_fail("settling-baseline-proof-barrier", 1800);
                return false;
            }
            return true;
        }

#if HORSE_ENABLE_GEKKONET
        static bool core_send(
            void* context,
            uint8_t remote_peer,
            const void* data,
            uint16_t bytes) noexcept
        {
            auto* runtime = static_cast<RollbackReplayForkRuntime*>(context);
            return runtime && remote_peer
                    == runtime->m_config.transport.remote_peer
                && runtime->m_network.enqueue(
                    RollbackProtocolV2PacketType::Gekko,
                    data, bytes, {}, runtime->m_handshake_generation);
        }

        static RollbackGekkoReceiveStatus core_receive(
            void* context,
            RollbackGekkoDatagram& out) noexcept
        {
            auto* runtime = static_cast<RollbackReplayForkRuntime*>(context);
            if (!runtime) return RollbackGekkoReceiveStatus::Fatal;
            RollbackUdpMessage message {};
            while (runtime->m_network.dequeue(message))
            {
                if (message.handshake_generation
                    != runtime->m_handshake_generation)
                    return RollbackGekkoReceiveStatus::Fatal;
                if (message.packet_type
                    == RollbackProtocolV2PacketType::Input)
                {
                    runtime->accept_peer_summary(message);
                    continue;
                }
                if (message.packet_type
                    == RollbackProtocolV2PacketType::LaunchBarrier)
                {
                    runtime->accept_terminal_barrier(message);
                    continue;
                }
                if (message.packet_type
                        == RollbackProtocolV2PacketType::Disconnect
                    || message.packet_type
                        == RollbackProtocolV2PacketType::Desync)
                    return RollbackGekkoReceiveStatus::Fatal;
                if (message.packet_type
                        != RollbackProtocolV2PacketType::Gekko
                    || message.payload_bytes == 0)
                    continue;
                out.remote_peer = runtime->m_config.transport.remote_peer;
                out.bytes = message.payload_bytes;
                std::memcpy(out.payload.data(), message.payload.data(),
                            message.payload_bytes);
                return RollbackGekkoReceiveStatus::Packet;
            }
            return RollbackGekkoReceiveStatus::Empty;
        }

        static bool core_game_event(
            void* context,
            GekkoGameEvent& event,
            const void*) noexcept
        {
            auto* runtime = static_cast<RollbackReplayForkRuntime*>(context);
            return runtime && runtime->process_game_event(event);
        }

        static bool core_idle_update(void* context) noexcept
        {
            return context != nullptr;
        }

        static void core_failure(
            void* context,
            const char* reason) noexcept
        {
            if (auto* runtime =
                    static_cast<RollbackReplayForkRuntime*>(context))
                runtime->m_core_failure_reason = reason
                    ? reason : "replay-fork-gekko-core-failure";
        }

        bool process_game_event(GekkoGameEvent& event) noexcept
        {
            switch (event.type)
            {
            case GekkoSaveEvent: return process_save(event);
            case GekkoLoadEvent: return process_load(event);
            case GekkoAdvanceEvent: return process_advance(event);
            default: return true;
            }
        }

        bool process_save(GekkoGameEvent& event) noexcept
        {
            if (!event.data.save.state || !event.data.save.state_len
                || !event.data.save.checksum)
                return false;
            uint32_t frame = 0;
            if (!RollbackGekkoStateFrameToKey(
                    event.data.save.frame, frame))
                return false;
            const bool baseline = event.data.save.frame
                == kRollbackGekkoBaselineFrame;
            if ((!baseline && (!m_current_frame.valid
                              || m_current_frame.value != frame))
                || (baseline && m_current_frame.valid
                    && m_current_frame.value != frame))
                return false;
            RollbackStepState state {};
            if (m_post_advance_frame.valid
                && m_post_advance_frame.value == frame)
                state = std::move(m_post_advance_state);
            else
            {
                const RollbackStepStateReport captured =
                    CaptureRollbackStepState(
                        NativeBinding::imageBase(), m_manifest, state,
                        RollbackLifecycleMode::StockOnlinePvp, true,
                        nullptr, nullptr, nullptr, 0, nullptr,
                        &m_palette_variant_writer_registry);
                if (!captured.ok) return false;
            }
            m_post_advance_state = {};
            m_post_advance_frame.clear();
            RollbackSnapshotHandle handle {};
            const RollbackSnapshotStoreReport saved = m_store.save(
                m_pair_epoch, frame, state.combined_hash,
                state.canonical_hash, std::move(state),
                m_current_frame.valid ? m_current_frame
                                      : RollbackFrameStamp::From(frame),
                m_config.transport.rollback_window, handle);
            if (!saved.ok) return false;
            std::memcpy(event.data.save.state, &handle, sizeof(handle));
            *event.data.save.state_len = sizeof(handle);
            *event.data.save.checksum = static_cast<uint32_t>(
                handle.canonical_hash ^ (handle.canonical_hash >> 32));
            ReplayTraceFields fields;
            append_common(fields);
            fields.uinteger("frame", frame)
                .boolean("baseline", baseline)
                .hex("canonical_hash", handle.canonical_hash)
                .hex("checksum", *event.data.save.checksum);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_save", fields);
            ++m_status.saves;
            m_status.snapshot_peak = (std::max)(
                m_status.snapshot_peak,
                static_cast<uint32_t>(m_store.occupied()));
            return true;
        }

        bool process_load(GekkoGameEvent& event) noexcept
        {
            if (!event.data.load.state
                || event.data.load.state_len
                    != sizeof(RollbackSnapshotHandle))
                return false;
            uint32_t frame = 0;
            if (!RollbackGekkoStateFrameToKey(
                    event.data.load.frame, frame))
                return false;
            RollbackSnapshotHandle handle {};
            std::memcpy(&handle, event.data.load.state, sizeof(handle));
            if (handle.frame != frame || handle.epoch != m_pair_epoch)
                return false;
            const RollbackStepState* state = nullptr;
            const RollbackSnapshotStoreReport loaded =
                m_store.load(handle, state);
            if (!loaded.ok || !state) return false;
            const RollbackStepStateReport restored = RestoreRollbackStepState(
                NativeBinding::imageBase(), *state, true,
                RollbackLifecycleMode::StockOnlinePvp, true, true,
                nullptr, nullptr, nullptr,
                &m_palette_variant_writer_registry);
            if (!restored.ok) return false;
            RollbackStepState verified {};
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                NativeBinding::imageBase(), m_manifest, verified,
                RollbackLifecycleMode::StockOnlinePvp, true,
                nullptr, nullptr, nullptr, 0, nullptr,
                &m_palette_variant_writer_registry);
            const RollbackHgCpuFrameCompare hgcpu_compare =
                CompareRollbackHgCpuFrames(state->hgcpu, verified.hgcpu);
            ReplayTraceFields load_fields;
            append_common(load_fields);
            load_fields.uinteger("frame", frame)
                .hex("expected_canonical_hash", handle.canonical_hash)
                .hex("verified_canonical_hash", verified.canonical_hash)
                .boolean("capture_ok", captured.ok)
                .boolean("hgcpu_policy_match", hgcpu_compare.policy_match)
                .uinteger("hgcpu_unignored_mismatches",
                    hgcpu_compare.unignored_mismatch_count)
                .boolean("hgcpu_topology_match",
                    hgcpu_compare.topology_match)
                .boolean("motion_bank_match",
                    hgcpu_compare.motion_bank_match)
                .boolean("motion_tail_match",
                    hgcpu_compare.motion_tail_match)
                .boolean("secondary_event_stack_match",
                    hgcpu_compare.secondary_event_stack_match)
                .boolean("timer_node_match",
                    hgcpu_compare.timer_node_match);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_load", load_fields);
            if (!captured.ok
                || verified.canonical_hash != handle.canonical_hash)
                return false;
            m_current_frame = frame;
            m_last_advance_canonical_hash = handle.canonical_hash;
            m_post_advance_state = {};
            m_post_advance_frame.clear();
            ++m_status.loads;
            return true;
        }

        bool process_advance(GekkoGameEvent& event) noexcept
        {
            const RollbackGekkoGameplayInputDecodeReport decoded =
                DecodeRollbackGekkoGameplayInputs(
                    event.data.adv.frame, event.data.adv.inputs,
                    event.data.adv.input_len, 2);
            if (!decoded.ok) return false;
            RollbackDecodedGameplayInput p0 {};
            RollbackDecodedGameplayInput p1 {};
            if (!GetRollbackGekkoDecodedGameplayInput(decoded, 0, p0)
                || !GetRollbackGekkoDecodedGameplayInput(decoded, 1, p1))
                return false;
            const uint32_t frame = decoded.frame;
            const bool advanced =
                ReplayScrub::instance().replay_fork_direct_advance(
                    p0.input_value, p1.input_value);
            if (!advanced)
                return false;
            RollbackStepState state {};
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                NativeBinding::imageBase(), m_manifest, state,
                RollbackLifecycleMode::StockOnlinePvp, true,
                nullptr, nullptr, nullptr, 0, nullptr,
                &m_palette_variant_writer_registry);
            if (!captured.ok) return false;
            m_last_advance_canonical_hash = state.canonical_hash;
            ReplayTraceFields advance_fields;
            append_common(advance_fields);
            advance_fields.uinteger("frame", frame)
                .hex("player0_input", p0.input_value)
                .hex("player1_input", p1.input_value)
                .boolean("rolling_back", event.data.adv.rolling_back)
                .boolean("running_ahead", event.data.adv.running_ahead)
                .hex("canonical_hash", state.canonical_hash)
                .hex("hgcpu_hash", state.hgcpu.canonical_hash)
                .hex("skeleton_runtime_hash",
                    state.hgcpu.skeleton_runtime_hash)
                .hex("motion_bank_hash",
                    state.hgcpu.motion_bank_hash)
                .hex("motion_tail_hash",
                    state.hgcpu.motion_tail_hash)
                .hex("secondary_event_stack_hash",
                    state.hgcpu.secondary_event_stack_hash)
                .hex("timer_node_hash",
                    state.hgcpu.timer_node_hash)
                .hex("explicit_hash",
                    state.explicit_snapshot.canonical_hash)
                .hex("stage_hash", state.breakable_stage.canonical_hash)
                .hex("wind_hash", state.stage_wind.canonical_hash)
                .uinteger("frame_counter", state.frame_counter);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_advance", advance_fields);
            // A delayed input can invalidate an earlier predicted summary.
            // Publish the corrected rollback frame as well; the same ring
            // slot is replaced and consensus is accepted only after the peer
            // presents the identical corrected state and decoded inputs.
            if (event.data.adv.rolling_back
                && !event.data.adv.running_ahead
                && frame < m_config.run_frames)
                publish_summary(frame, state.canonical_hash,
                                p0.input_value, p1.input_value);
            m_current_frame = frame;
            m_post_advance_state = state;
            m_post_advance_frame = frame;
            ++m_status.advances;
            const size_t prediction_slot = frame & 127u;
            if (!event.data.adv.rolling_back)
            {
                m_prediction_hashes[prediction_slot] =
                    state.canonical_hash;
                m_prediction_frames[prediction_slot] = frame;
                m_prediction_hash_valid[prediction_slot] = true;
            }
            else if (m_prediction_hash_valid[prediction_slot]
                && m_prediction_frames[prediction_slot] == frame
                && m_prediction_hashes[prediction_slot]
                    != state.canonical_hash)
            {
                m_status.prediction_diverged = true;
            }
            if (event.data.adv.rolling_back)
                ++m_status.rollback_advances;
            if (!event.data.adv.rolling_back
                && !event.data.adv.running_ahead)
            {
                ++m_status.completed_frames;
                m_status.final_hash = state.canonical_hash;
                if (m_status.completed_frames == m_config.run_frames)
                {
                    m_final_frame = frame;
                    m_final_summary_accepted = false;
                    m_terminal_summary_conflict = false;
                }
                publish_summary(frame, state.canonical_hash,
                                p0.input_value, p1.input_value);
            }
            return true;
        }
#else
        static bool core_send(
            void*, uint8_t, const void*, uint16_t) noexcept
        {
            return false;
        }

        static RollbackGekkoReceiveStatus core_receive(
            void*, RollbackGekkoDatagram&) noexcept
        {
            return RollbackGekkoReceiveStatus::Fatal;
        }

        static bool core_game_event(
            void*, GekkoGameEvent&, const void*) noexcept
        {
            return false;
        }

        static bool core_idle_update(void*) noexcept { return false; }

        static void core_failure(void* context, const char*) noexcept
        {
            if (auto* runtime =
                    static_cast<RollbackReplayForkRuntime*>(context))
            {
                runtime->m_core_failure_reason = "gekkonet-disabled";
            }
        }
#endif

        void record_owned_input(uint32_t input) noexcept
        {
            // Keep submission count for diagnostics. Acceptance hashes are
            // accumulated only after both peers accept the corrected decoded
            // inputs for a frame, so connection-bootstrap calls and predicted
            // Advances cannot skew cross-peer evidence.
            (void)input;
            ++m_owned_input_count;
        }

        static uint64_t hash_input_sample(
            uint32_t frame, uint32_t value) noexcept
        {
            uint64_t mixed = (static_cast<uint64_t>(frame) << 32) | value;
            mixed += 0x9E3779B97F4A7C15ull;
            mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
            mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;
            return mixed ^ (mixed >> 31);
        }

        void publish_summary(
            uint32_t frame,
            uint64_t canonical_hash,
            uint32_t p0,
            uint32_t p1) noexcept
        {
            RollbackReplayForkFrameSummary summary {};
            summary.frame = frame;
            summary.pair_epoch = m_pair_epoch;
            summary.canonical_hash = canonical_hash;
            summary.input[0] = p0;
            summary.input[1] = p1;
            summary.owned_input_hash = m_status.local_input_hash;
            summary.owned_input_count = m_owned_input_count;
            summary.owned_player_slot =
                m_config.transport.local_player_slot;
            if (m_final_frame.valid && m_final_frame.value == frame)
            {
                m_terminal_summary = summary;
                m_terminal_summary_valid = true;
            }
            const size_t slot = frame;
            if (slot >= m_local_summaries.size())
            {
                m_core_failure_reason =
                    "replay-fork-local-evidence-frame-out-of-range";
                return;
            }
            m_local_summaries[slot] = summary;
            m_local_summary_valid[slot] = true;
            m_terminal_evidence_dirty = true;
            (void)m_network.enqueue(
                RollbackProtocolV2PacketType::Input,
                &summary, sizeof(summary), {}, m_handshake_generation);
            match_summary(frame);
        }

        void accept_peer_summary(const RollbackUdpMessage& message) noexcept
        {
            if (message.payload_bytes
                != sizeof(RollbackReplayForkFrameSummary))
            {
                m_core_failure_reason =
                    "replay-fork-peer-summary-size-mismatch";
                return;
            }
            RollbackReplayForkFrameSummary summary {};
            std::memcpy(&summary, message.payload.data(), sizeof(summary));
            if (summary.magic != 0x324C4652u
                || summary.pair_epoch != m_pair_epoch
                || summary.canonical_hash == 0
                || summary.owned_player_slot
                    != (m_config.transport.local_player_slot ^ 1u))
            {
                m_core_failure_reason = "replay-fork-peer-summary-invalid";
                return;
            }
            const size_t slot = summary.frame;
            if (slot >= m_remote_summaries.size())
            {
                m_core_failure_reason =
                    "replay-fork-remote-evidence-frame-out-of-range";
                return;
            }
            m_remote_summaries[slot] = summary;
            m_remote_summary_valid[slot] = true;
            match_summary(summary.frame);
        }

        void match_summary(uint32_t frame) noexcept
        {
            const size_t slot = frame;
            if (slot >= m_local_summaries.size()
                || slot >= m_remote_summaries.size())
            {
                m_core_failure_reason =
                    "replay-fork-evidence-match-frame-out-of-range";
                return;
            }
            if (!m_local_summary_valid[slot]
                || !m_remote_summary_valid[slot])
                return;
            const RollbackReplayForkFrameSummary& local =
                m_local_summaries[slot];
            const RollbackReplayForkFrameSummary& remote =
                m_remote_summaries[slot];
            if (local.frame != frame || remote.frame != frame
                || local.canonical_hash != remote.canonical_hash
                || local.input[0] != remote.input[0]
                || local.input[1] != remote.input[1])
            {
                // Either side may still hold a predicted summary for this
                // frame. Keep both slots live so a later rollback Advance can
                // replace the local value. The terminal barrier fails closed
                // if no corrected match ever arrives.
                if (m_final_frame.valid && m_final_frame.value == frame)
                    m_terminal_summary_conflict = true;
                return;
            }
            ++m_status.pair_accepts;
            m_status.peer_final_hash = remote.canonical_hash;
            if (m_final_frame.valid && m_final_frame.value == frame)
            {
                if (local.owned_player_slot
                        != m_config.transport.local_player_slot
                    || remote.owned_player_slot
                        != (m_config.transport.local_player_slot ^ 1u))
                {
                    m_core_failure_reason =
                        "replay-fork-terminal-input-proof-mismatch";
                    return;
                }
                // The first non-rollback Advance at the terminal frame can be
                // predicted. A later rollback Advance replaces its summary,
                // but service_advance deliberately does not count rollback
                // Advances as newly completed frames. Promote the mutually
                // accepted corrected summary here so the terminal proof never
                // authenticates a stale predicted m_status.final_hash.
                m_status.final_hash = local.canonical_hash;
                m_status.peer_final_hash = remote.canonical_hash;
                m_final_summary_accepted = true;
                m_terminal_summary_conflict = false;
            }
            m_local_summary_valid[slot] = false;
            m_remote_summary_valid[slot] = false;
        }

        bool network_valid() const noexcept
        {
            const RollbackUdpWorkerStatus status = m_network.status();
            return status.running && status.endpoint_open
                && status.endpoint_pinned && status.peer_ready
                && status.failure == RollbackUdpWorkerFailure::None
                && status.handshake_generation == m_handshake_generation;
        }

        void begin_resume_verification() noexcept
        {
            m_gekko.shutdown();
            m_network.stop();
            if (!restore_anchor("replay-fork-final-restore")) return;
            m_status.baseline_restored = true;
            m_resume_observation =
                ReplayScrub::instance().replay_fork_lab_observation();
            ReplayScrub::instance().exit_replay_fork_lab_hold(true);
            ReplayTraceFields cleanup;
            append_common(cleanup);
            cleanup.boolean("anchor_restore_ok", true)
                .boolean("hold_released", true)
                .boolean("resume_requested", true)
                .boolean("replay_resumed", false);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_cleanup", cleanup);
            transition(RollbackReplayForkState::VerifyingResume,
                       "verifying-replay-resume");
        }

        void service_resume_verification() noexcept
        {
            const ReplayScrub::ReplayForkLabObservation observed =
                ReplayScrub::instance().replay_fork_lab_observation();
            if (observed.valid && m_resume_observation.valid
                && observed.master > m_resume_observation.master
                && observed.frame_counter
                    > m_resume_observation.frame_counter)
            {
                m_status.replay_resumed = true;
                complete();
                return;
            }
            wait_or_fail("replay-resume-not-observed", 600);
        }

        void complete() noexcept
        {
            ReplayTraceFields fields;
            append_common(fields);
            fields.hex("baseline_hash", m_status.baseline_hash)
                .hex("final_hash", m_status.final_hash)
                .hex("peer_final_hash", m_status.peer_final_hash)
                .uinteger("frames", m_status.completed_frames)
                .uinteger("saves", m_status.saves)
                .uinteger("loads", m_status.loads)
                .uinteger("advances", m_status.advances)
                .uinteger("rollback_advances", m_status.rollback_advances)
                .uinteger("pair_accepts", m_status.pair_accepts)
                .uinteger("evidence_frames", m_status.evidence_frames)
                .uinteger("summary_overwrites",
                    m_status.summary_overwrites)
                .hex("terminal_evidence_hash",
                    m_status.terminal_evidence_hash)
                .uinteger("presentation_syncs",
                    m_status.presentation_syncs)
                .uinteger("presentation_skips",
                    m_status.presentation_skips)
                .uinteger("presentation_failures",
                    m_status.presentation_failures)
                .uinteger("snapshot_peak", m_status.snapshot_peak)
                .hex("local_input_hash", m_status.local_input_hash)
                .hex("remote_input_hash", m_status.remote_input_hash)
                .boolean("no_desync", m_status.no_desync)
                .boolean("prediction_diverged",
                    m_status.prediction_diverged)
                .boolean("presentation_gameplay_unchanged",
                    m_status.presentation_gameplay_unchanged)
                .boolean("presentation_motion_observed",
                    m_status.presentation_motion_observed)
                .boolean("baseline_restored", m_status.baseline_restored)
                .boolean("replay_resumed", m_status.replay_resumed)
                .boolean("passed", true);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_complete", fields);
            m_status.state = RollbackReplayForkState::Complete;
            m_status.failure = "ok";
            m_store.clear();
            m_anchor = {};
            m_anchor_captured = false;
            emit_status(true);
        }

        void fail(const char* reason) noexcept
        {
            if (m_status.state == RollbackReplayForkState::Fatal) return;
            m_status.state = RollbackReplayForkState::Fatal;
            m_status.failure = reason ? reason : "replay-fork-failure";
            m_status.no_desync = m_gekko.desync_events() == 0;
            const RollbackUdpWorkerStatus network = m_network.status();
            ReplayTraceFields fields;
            append_common(fields);
            fields.string("failure", m_status.failure)
                .integer("gekko_desync_frame", m_gekko.last_desync_frame())
                .hex("gekko_local_checksum",
                    m_gekko.last_desync_local_checksum())
                .hex("gekko_remote_checksum",
                    m_gekko.last_desync_remote_checksum())
                .string("udp_failure",
                    RollbackUdpWorkerFailureName(network.failure))
                .boolean("udp_running", network.running)
                .boolean("udp_endpoint_open", network.endpoint_open)
                .boolean("udp_endpoint_pinned", network.endpoint_pinned)
                .boolean("udp_peer_ready", network.peer_ready)
                .uinteger("udp_packets_sent", network.packets_sent)
                .uinteger("udp_packets_received", network.packets_received)
                .uinteger("udp_packets_authenticated",
                    network.packets_authenticated)
                .uinteger("udp_packets_rejected", network.packets_rejected)
                .uinteger("udp_handshake_generation",
                    network.handshake_generation)
                .boolean("passed", false);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_failed", fields);
            shutdown(true);
            m_status.state = RollbackReplayForkState::Fatal;
            m_status.failure = reason ? reason : "replay-fork-failure";
        }

        void append_common(ReplayTraceFields& fields) const noexcept
        {
            fields.string("session_domain", "replay-fork-lab")
                .string("request_id", m_config.request_id)
                .string("client_role", m_config.client_role)
                .string("evidence_scope", "rollback-core-integration")
                .boolean("production_certified", false)
                .boolean("proof_non_skip", false)
                .uinteger("local_player_slot",
                    m_config.transport.local_player_slot)
                .uinteger("remote_player_slot",
                    1u - m_config.transport.local_player_slot)
                .hex("replay_run_nonce_hash",
                    m_config.transport.replay_run_nonce_hash)
                .hex("executable_id", m_status.executable_id)
                .hex("expected_build_id",
                    m_config.transport.expected_build_id)
                .hex("schema_id", m_manifest.schema_hash())
                .hex("expected_schema_id",
                    m_config.transport.expected_schema_id);
        }

        void emit_status(bool force) noexcept
        {
            if (!force && (m_status.service_ticks % 30u) != 0u) return;
            ReplayTraceFields fields;
            const RollbackUdpWorkerStatus network = m_network.status();
            append_common(fields);
            fields.string("state",
                    RollbackReplayForkStateName(m_status.state))
                .string("failure", m_status.failure)
                .uinteger("service_ticks", m_status.service_ticks)
                .hex("baseline_hash", m_status.baseline_hash)
                .hex("final_hash", m_status.final_hash)
                .uinteger("direct_windows",
                    m_status.completed_direct_windows)
                .uinteger("frames", m_status.completed_frames)
                .uinteger("snapshot_peak", m_status.snapshot_peak)
                .uinteger("saves", m_status.saves)
                .uinteger("loads", m_status.loads)
                .uinteger("advances", m_status.advances)
                .uinteger("rollback_advances",
                    m_status.rollback_advances)
                .uinteger("pair_accepts", m_status.pair_accepts)
                .uinteger("evidence_frames", m_status.evidence_frames)
                .uinteger("summary_overwrites",
                    m_status.summary_overwrites)
                .hex("terminal_evidence_hash",
                    m_status.terminal_evidence_hash)
                .uinteger("presentation_syncs",
                    m_status.presentation_syncs)
                .uinteger("presentation_skips",
                    m_status.presentation_skips)
                .uinteger("presentation_failures",
                    m_status.presentation_failures)
                .boolean("hold_stable", m_status.hold_stable)
                .boolean("baseline_restored",
                    m_status.baseline_restored)
                .boolean("replay_resumed", m_status.replay_resumed)
                .boolean("gekko_started", m_status.gekko_started)
                .boolean("prediction_diverged",
                    m_status.prediction_diverged)
                .boolean("presentation_gameplay_unchanged",
                    m_status.presentation_gameplay_unchanged)
                .boolean("presentation_motion_observed",
                    m_status.presentation_motion_observed)
                .string("udp_failure",
                    RollbackUdpWorkerFailureName(network.failure))
                .boolean("udp_running", network.running)
                .boolean("udp_endpoint_open", network.endpoint_open)
                .boolean("udp_endpoint_pinned", network.endpoint_pinned)
                .boolean("udp_peer_ready", network.peer_ready)
                .uinteger("udp_packets_sent", network.packets_sent)
                .uinteger("udp_packets_received", network.packets_received)
                .uinteger("udp_packets_authenticated",
                    network.packets_authenticated)
                .uinteger("udp_packets_rejected", network.packets_rejected)
                .uinteger("udp_handshake_generation",
                    network.handshake_generation)
                .boolean("no_desync", m_status.no_desync);
            ReplayDebugTrace::instance().event(
                "rollback_replay_fork_status", fields);
        }

        RollbackReplayForkConfig m_config {};
        RollbackReplayForkStatus m_status {};
        RollbackSnapshotManifest m_manifest {};
        RollbackLifecycleEpoch m_lab_epoch {};
        RollbackReplayInputScript m_script {};
        RollbackStepState m_anchor {};
        RollbackSnapshotStore<RollbackStepState> m_store {};
        RollbackGekkoRuntimeCore m_gekko {};
        RollbackUdpNetworkWorker m_network {};
        RollbackFrameStamp m_current_frame {};
        RollbackStepState m_post_advance_state {};
        RollbackPaletteVariantWriterRegistry
            m_palette_variant_writer_registry {};
        RollbackFrameStamp m_post_advance_frame {};
        ReplayScrub::ReplayForkLabObservation m_anchor_observation {};
        ReplayScrub::ReplayForkLabObservation m_resume_observation {};
        std::vector<RollbackReplayForkFrameSummary> m_local_summaries {};
        std::vector<RollbackReplayForkFrameSummary> m_remote_summaries {};
        std::vector<uint8_t> m_local_summary_valid {};
        std::vector<uint8_t> m_remote_summary_valid {};
        std::array<uint32_t, 128> m_prediction_frames {};
        std::array<uint64_t, 128> m_prediction_hashes {};
        std::array<bool, 128> m_prediction_hash_valid {};
        bool m_anchor_captured {false};
        bool m_network_started {false};
        bool m_baseline_proof_sent {false};
        bool m_baseline_proof_accepted {false};
        bool m_baseline_ack_accepted {false};
        bool m_terminal_proof_sent {false};
        bool m_terminal_proof_accepted {false};
        bool m_terminal_ack_accepted {false};
        bool m_seek_requested {false};
        uint32_t m_hold_settle_ticks {0};
        uint32_t m_stability_observations {0};
        size_t m_direct_window_index {0};
        size_t m_anchor_input_index {0};
        size_t m_run_input_index {0};
        uint32_t m_owned_input_count {0};
        uint32_t m_gekko_bootstrap_submissions {0};
        RollbackFrameStamp m_final_frame {};
        RollbackReplayForkFrameSummary m_terminal_summary {};
        bool m_final_summary_accepted {false};
        bool m_terminal_summary_valid {false};
        bool m_terminal_summary_conflict {false};
        bool m_terminal_evidence_dirty {true};
        uint64_t m_state_start_tick {0};
        uint64_t m_pair_epoch {0};
        uint64_t m_handshake_generation {0};
        uint64_t m_baseline_barrier_complete_tick {0};
        uint64_t m_terminal_barrier_complete_tick {0};
        uint64_t m_last_advance_canonical_hash {0};
        uint64_t m_last_presentation_hash {0};
        NativeFTransform48 m_last_published_transform[2] {};
        Lux m_lux {};
        Fn m_actor_location_fn {};
        void* m_presentation_actor[2] {};
        int32_t m_presentation_actor_array_index[2] {-1, -1};
        int32_t m_presentation_actor_array_count {0};
        bool m_last_presentation_valid {false};
        const char* m_core_failure_reason {nullptr};
        const char* m_epoch_failure {"replay-fork-epoch-not-run"};
    };
}
