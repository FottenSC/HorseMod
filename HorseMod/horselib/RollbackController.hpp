// ============================================================================
// Horse::RollbackController
//
// Local rollback controller shell. It owns configuration, counters, snapshot
// probes, and active-round deterministic resimulation probes.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "RollbackDiag.hpp"
#include "RollbackEndToEndHarness.hpp"
#include "RollbackFaultInject.hpp"
#include "RollbackGekkoAdapter.hpp"
#include "RollbackGekkoGameplayInputBridge.hpp"
#include "RollbackGekkoUdpAdapter.hpp"
#include "RollbackHgCpuSnapshot.hpp"
#include "RollbackInputLogProbe.hpp"
#include "RollbackInputHistory.hpp"
#include "RollbackLiveBoundaryHook.hpp"
#include "RollbackLiveActivationExecutor.hpp"
#include "RollbackLiveActivationGate.hpp"
#include "RollbackLiveOnlineCapture.hpp"
#include "RollbackLivePeerPipeline.hpp"
#include "RollbackLiveTransportQueue.hpp"
#include "RollbackLifecycle.hpp"
#include "RollbackOnlineSession.hpp"
#include "RollbackP2PHarness.hpp"
#include "RollbackProductionRuntime.hpp"
#include "RollbackReplayForkRuntime.hpp"
#include "RollbackSidecar.hpp"
#include "RollbackSnapshot.hpp"
#include "RollbackStepHarness.hpp"
#include "RollbackStockTransportObserveHook.hpp"
#include "RollbackStockTransportSurface.hpp"
#include "RollbackTransport.hpp"

#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Horse
{
    enum class RollbackLabCase : uint8_t
    {
        Disabled,
        BaselineOracle,
        SnapshotRoundTrip,
        DelayedInputCorrection,
        ResimMatrix,
        CacheOwnershipTrace,
        OnlineSessionSelfTest,
        LiveTransportSelfTest,
        LivePeerPipelineSelfTest,
        EndToEndSelfTest,
        LiveActivationSelfTest,
        LiveActivationExecutorSelfTest,
        GekkoGameplayInputSelfTest,
        OnlineBoundaryTrace,
        CacheInjectionTrace,
        CachePredictionTrace,
        GekkoSessionSelfTest,
        GekkoAdapterSelfTest,
        GekkoUdpSelfTest,
        StockTransportSelfTest,
        StockTransportObserve,
        LiveOnlineCapture,
        SideEffectLedger,
        ReplayForkLab,
        Production,
    };

    struct RollbackLabConfig
    {
        bool enabled {false};
        bool trace_enabled {false};
        bool native_lifecycle_trace_frame_input_log_only {false};
        RollbackLabCase test_case {RollbackLabCase::BaselineOracle};
        uint32_t rollback_window {12};
        uint32_t seed {0x5C6B0001u};
        bool live_activation_operator_enable {false};
        uint8_t live_activation_source_peer {0xA0u};
        uint8_t live_activation_destination_peer {0xB0u};
        uint64_t live_activation_session_id {0x4C495645414354ull};
        std::string client_role;
        std::string sandbox_root;
        std::string sandbox_box;
        uint8_t local_peer_id {0};
        uint8_t remote_peer_id {0};
        uint16_t sidecar_local_port {0};
        uint16_t sidecar_remote_port {0};
        std::string sidecar_remote_addr {"127.0.0.1"};
        std::string activation_token;
        bool observe_gameflow_requested {false};
        bool observe_gameflow_process_events {false};
        bool online_stage_requested {false};
        bool online_stage_cleanup_only {false};
        bool production_logical_release_only {false};
        bool online_stage_wait_host_room_ready_marker {false};
        int32_t online_stage_main_user_id_override {-1};
        int32_t launch_left_character_override {-1};
        int32_t launch_right_character_override {-1};
        std::string launch_left_character_code_override;
        std::string launch_right_character_code_override;
        int32_t launch_stage_override {-1};
        int32_t launch_rounds_to_win_override {3};
        std::string online_stage_native_session_name;
        std::string online_stage_session_name;
        std::string online_stage_room_name;
        std::string online_stage_host_room_ready_marker;
        std::string online_stage_goal {"player-match-battle"};
        std::string replay_input_file;
        std::string main_menu_player_match_route;
        RollbackStockJoinRoute stock_join_route {
            RollbackStockJoinRoute::InjectedStockInvite};
        uint8_t local_replay_player {0};
        uint8_t remote_replay_player {1};
        uint32_t replay_divergence_frame {120};
        uint32_t replay_divergence_window {12};
        uint32_t replay_fork_stability_ticks {120};
        uint32_t replay_fork_run_frames {600};
        bool replay_fork_require_rollback {false};
        bool replay_fork_requested {false};
        std::string output_path;
        std::string request_id;
        uint64_t request_generation {0};
        std::string source {"default"};
        uint32_t qualification_contract_version {0};
        std::string qualification_run_id;
        std::string qualification_case_id;
        std::string qualification_segment_id;
        std::string qualification_schedule_hash;
        std::string qualification_content_sha256;
        uint32_t qualification_protocol_version {0};
        uint32_t qualification_snapshot_version {0};
        RollbackProductionConfig production {};

        bool qualification_enabled() const noexcept
        {
            return qualification_contract_version != 0
                && !qualification_run_id.empty()
                && !qualification_case_id.empty()
                && !qualification_segment_id.empty()
                && qualification_schedule_hash.size() == 64
                && qualification_protocol_version != 0
                && qualification_snapshot_version != 0
                && !client_role.empty();
        }

        bool sidecar_requested() const noexcept
        {
            return local_peer_id != 0
                && remote_peer_id != 0
                && local_peer_id != remote_peer_id
                && sidecar_local_port != 0
                && sidecar_remote_port != 0
                && live_activation_session_id != 0
                && !activation_token.empty();
        }
    };

    static inline void NormalizeRollbackProductionConfig(
        RollbackLabConfig& cfg) noexcept
    {
        if (cfg.rollback_window == 0) cfg.rollback_window = 1;
        if (cfg.rollback_window > 60) cfg.rollback_window = 60;
        if (cfg.test_case != RollbackLabCase::Production
            && cfg.test_case != RollbackLabCase::ReplayForkLab)
            return;
        cfg.production.enabled = cfg.enabled && cfg.production.enabled;
        cfg.production.rollback_window =
            static_cast<uint16_t>(cfg.rollback_window);
        cfg.production.request_id = cfg.request_id;
        cfg.production.client_role = cfg.client_role;
        cfg.production.replay_trace_input_only =
            cfg.native_lifecycle_trace_frame_input_log_only;
        const auto expected_selection =
            RollbackStockOnlineLabDriver::desired(
                cfg.launch_left_character_override,
                cfg.launch_right_character_override,
                cfg.launch_stage_override,
                cfg.launch_rounds_to_win_override);
        if (!cfg.launch_left_character_code_override.empty()
            || !cfg.launch_right_character_code_override.empty())
        {
            const RollbackStockOnlineLabDriver::Selection coded_selection {
                cfg.launch_left_character_code_override.empty()
                    ? expected_selection.left_character
                    : cfg.launch_left_character_code_override,
                cfg.launch_right_character_code_override.empty()
                    ? expected_selection.right_character
                    : cfg.launch_right_character_code_override,
                expected_selection.stage,
                expected_selection.rounds_to_win,
            };
            cfg.production.expected_selection_hash =
                RollbackStockOnlineLabDriver::selection_hash(
                    coded_selection);
        }
        else
        {
            cfg.production.expected_selection_hash =
                RollbackStockOnlineLabDriver::selection_hash(
                    expected_selection);
        }
        if (cfg.test_case == RollbackLabCase::ReplayForkLab)
        {
            cfg.production.session_domain =
                RollbackSessionDomain::ReplayForkLab;
            return;
        }
        cfg.production.replay_input_file = cfg.replay_input_file;
        if (cfg.launch_stage_override >= 0)
        {
            cfg.production.expected_native_stage_identity =
                0x10000u
                | (static_cast<uint32_t>(cfg.launch_stage_override)
                    & 0xFFFFu);
        }
    }

    static inline const char* rollback_case_name(RollbackLabCase c) noexcept
    {
        switch (c)
        {
        case RollbackLabCase::Disabled: return "disabled";
        case RollbackLabCase::BaselineOracle: return "baseline-oracle";
        case RollbackLabCase::SnapshotRoundTrip: return "snapshot-roundtrip";
        case RollbackLabCase::DelayedInputCorrection: return "delayed-input";
        case RollbackLabCase::ResimMatrix: return "resim-matrix";
        case RollbackLabCase::CacheOwnershipTrace: return "cache-ownership";
        case RollbackLabCase::OnlineSessionSelfTest: return "online-session";
        case RollbackLabCase::LiveTransportSelfTest: return "live-transport";
        case RollbackLabCase::LivePeerPipelineSelfTest:
            return "live-peer-pipeline";
        case RollbackLabCase::EndToEndSelfTest:
            return "end-to-end";
        case RollbackLabCase::LiveActivationSelfTest:
            return "live-activation";
        case RollbackLabCase::LiveActivationExecutorSelfTest:
            return "live-activation-executor";
        case RollbackLabCase::GekkoGameplayInputSelfTest:
            return "gekko-gameplay-input";
        case RollbackLabCase::OnlineBoundaryTrace: return "online-boundary";
        case RollbackLabCase::CacheInjectionTrace: return "cache-injection";
        case RollbackLabCase::CachePredictionTrace: return "cache-prediction";
        case RollbackLabCase::GekkoSessionSelfTest: return "gekko-session";
        case RollbackLabCase::GekkoAdapterSelfTest: return "gekko-adapter";
        case RollbackLabCase::GekkoUdpSelfTest: return "gekko-udp";
        case RollbackLabCase::StockTransportSelfTest:
            return "stock-transport";
        case RollbackLabCase::StockTransportObserve:
            return "stock-observe";
        case RollbackLabCase::LiveOnlineCapture:
            return "live-online-capture";
        case RollbackLabCase::SideEffectLedger: return "side-effect-ledger";
        case RollbackLabCase::ReplayForkLab: return "replay-fork-lab";
        case RollbackLabCase::Production: return "production";
        default: return "unknown";
        }
    }

    static inline RollbackLabCase rollback_case_from_string(
        const std::string& s) noexcept
    {
        if (s == "baseline" || s == "baseline-oracle")
            return RollbackLabCase::BaselineOracle;
        if (s == "snapshot" || s == "snapshot-roundtrip")
            return RollbackLabCase::SnapshotRoundTrip;
        if (s == "delay" || s == "delayed-input")
            return RollbackLabCase::DelayedInputCorrection;
        if (s == "matrix" || s == "resim-matrix" || s == "rollback-matrix")
            return RollbackLabCase::ResimMatrix;
        if (s == "cache" || s == "cache-ownership")
            return RollbackLabCase::CacheOwnershipTrace;
        if (s == "online-session" || s == "session"
            || s == "online-session-selftest")
            return RollbackLabCase::OnlineSessionSelfTest;
        if (s == "live-transport" || s == "live-transport-queue"
            || s == "transport-queue" || s == "online-transport")
            return RollbackLabCase::LiveTransportSelfTest;
        if (s == "live-peer-pipeline" || s == "peer-pipeline"
            || s == "online-peer-pipeline" || s == "rollback-peer-pipeline")
            return RollbackLabCase::LivePeerPipelineSelfTest;
        if (s == "end-to-end" || s == "rollback-end-to-end"
            || s == "e2e" || s == "rollback-e2e"
            || s == "online-rollback-end-to-end")
            return RollbackLabCase::EndToEndSelfTest;
        if (s == "live-activation" || s == "live-rollback-activation"
            || s == "online-activation" || s == "rollback-activation"
            || s == "guarded-live-activation")
            return RollbackLabCase::LiveActivationSelfTest;
        if (s == "live-activation-executor"
            || s == "live-rollback-executor"
            || s == "online-activation-executor"
            || s == "guarded-live-executor")
            return RollbackLabCase::LiveActivationExecutorSelfTest;
        if (s == "gekko-gameplay-input" || s == "gekkonet-gameplay-input"
            || s == "gekko-input-bridge" || s == "gekkonet-input-bridge")
            return RollbackLabCase::GekkoGameplayInputSelfTest;
        if (s == "online-boundary" || s == "live-boundary"
            || s == "online-boundary-trace" || s == "live-input-boundary")
            return RollbackLabCase::OnlineBoundaryTrace;
        if (s == "cache-injection" || s == "input-cache-injection"
            || s == "cache-write-read" || s == "live-cache-injection")
            return RollbackLabCase::CacheInjectionTrace;
        if (s == "cache-prediction" || s == "input-cache-prediction"
            || s == "cache-non-idempotent"
            || s == "live-cache-prediction")
            return RollbackLabCase::CachePredictionTrace;
        if (s == "gekko" || s == "gekko-session"
            || s == "gekkonet" || s == "gekkonet-session")
            return RollbackLabCase::GekkoSessionSelfTest;
        if (s == "gekko-adapter" || s == "gekkonet-adapter"
            || s == "gekko-loopback" || s == "gekkonet-loopback")
            return RollbackLabCase::GekkoAdapterSelfTest;
        if (s == "gekko-udp" || s == "gekkonet-udp"
            || s == "gekko-socket" || s == "gekkonet-socket"
            || s == "gekko-udp-loopback")
            return RollbackLabCase::GekkoUdpSelfTest;
        if (s == "stock-transport" || s == "transport-surface"
            || s == "stock-transport-surface"
            || s == "online-transport-surface")
            return RollbackLabCase::StockTransportSelfTest;
        if (s == "stock-observe" || s == "stock-send-observe"
            || s == "stock-transport-observe"
            || s == "online-send-observe")
            return RollbackLabCase::StockTransportObserve;
        if (s == "live-online-capture" || s == "online-capture"
            || s == "live-capture" || s == "live-online-readiness"
            || s == "online-rollback-capture")
            return RollbackLabCase::LiveOnlineCapture;
        if (s == "side-effects" || s == "side-effect-ledger")
            return RollbackLabCase::SideEffectLedger;
        if (s == "replay-fork-lab" || s == "replay-fork")
            return RollbackLabCase::ReplayForkLab;
        if (s == "production" || s == "udp-production"
            || s == "gekko-production")
            return RollbackLabCase::Production;
        return RollbackLabCase::BaselineOracle;
    }

    static inline RollbackLifecycleMode rollback_lifecycle_mode_from_string(
        const std::string& value) noexcept
    {
        (void)value;
        return RollbackLifecycleMode::StockOnlinePvp;
    }

    class RollbackController
    {
    public:
        void configure(RollbackLabConfig cfg) noexcept
        {
            NormalizeRollbackProductionConfig(cfg);
            auto& production = RollbackProductionRuntime::instance();
            const RollbackProductionState state =
                production.status().state;
            const bool cleanup_request =
                cfg.test_case == RollbackLabCase::Production
                && cfg.online_stage_requested
                && cfg.online_stage_cleanup_only;
            const bool continuing_cleanup_handoff =
                cleanup_request && m_cleanup_handoff_active;
            const RollbackDiagnosticReleaseDecision release_decision =
                EvaluateRollbackDiagnosticRelease(
                    cfg.production_logical_release_only,
                    m_config.test_case == RollbackLabCase::Production,
                    m_config.production.native_correction_only,
                    cfg.test_case == RollbackLabCase::Production,
                    cfg.production.native_correction_only,
                    cfg.production.enabled);
            if (release_decision
                == RollbackDiagnosticReleaseDecision::Reject)
            {
                if (production.owns_tick_boundary())
                    production.request_fail_closed(
                        "diagnostic-logical-release-rejected");
                else
                    production.reject_configuration(
                        "diagnostic-logical-release-rejected");
                return;
            }
            if (release_decision
                == RollbackDiagnosticReleaseDecision::Release)
            {
                // Logical release is immediate only when no pre-control
                // SimulationLoop iteration is held. A held iteration remains
                // fail-closed until frame 0 or verified stock cleanup.
                emit_qualification_terminal();
                production.shutdown();
            }
            const bool duplicate_active_request =
                !cleanup_request
                &&
                cfg.test_case == RollbackLabCase::Production
                && m_config.test_case == RollbackLabCase::Production
                && cfg.request_generation != 0
                && cfg.request_generation == m_config.request_generation
                && cfg.request_id == m_config.request_id
                && RollbackProductionConfigEquivalent(
                    cfg.production, m_config.production)
                && state != RollbackProductionState::Fatal
                && state != RollbackProductionState::Stopping;
            if (duplicate_active_request)
            {
                // Phase workers can rewrite an immutable request to obtain
                // a fresh acknowledgement. Never reset its live state.
                RollbackDiag::emit_configured(m_config, &m_manifest);
                return;
            }
            if (production.owns_tick_boundary())
            {
                try
                {
                    m_pending_config = std::move(cfg);
                }
                catch (...)
                {
                    production.request_fail_closed(
                        "pending-config-allocation-failed");
                    return;
                }
                if (cleanup_request)
                {
                    if (!m_cleanup_handoff_active)
                    {
                        if (!arm_cleanup_handoff(*m_pending_config))
                            return;
                    }
                    production.request_return_to_lobby();
                    return;
                }
                production.request_fail_closed("operator-reconfigure");
                return;
            }
            try
            {
            m_config = std::move(cfg);
            m_manifest = BuildInitialRollbackManifest(
                NativeBinding::imageBase(), m_config.rollback_window);
            if (m_config.source == "beta-config")
            {
                // Persistent beta profiles deliberately do not hardcode
                // build/schema/stage/selection constants. Bind the immutable
                // local binary contracts here; the authenticated handshake
                // still rejects a peer with different derived values. Stage
                // and selection bind later to the actual stock Steam lobby
                // and frozen native round identity on both peers.
                m_config.production.expected_build_id =
                    ComputeRollbackExecutableId(NativeBinding::imageBase());
                m_config.production.expected_schema_id =
                    m_manifest.schema_hash();
                m_config.production.expected_native_stage_identity = 0;
                m_config.production.expected_selection_hash = 0;
                m_config.production.bind_observed_stock_selection = true;
            }
            if (m_config.test_case == RollbackLabCase::Production)
            {
                RollbackReplayForkRuntime::instance().shutdown(true);
                RollbackProductionRuntime::instance().configure(
                    m_config.production);
            }
            else if (m_config.test_case == RollbackLabCase::ReplayForkLab)
            {
                RollbackProductionRuntime::instance().shutdown();
                RollbackReplayForkConfig replay {};
                replay.enabled = m_config.enabled;
                replay.transport = m_config.production;
                replay.replay_input_file = m_config.replay_input_file;
                replay.request_id = m_config.request_id;
                replay.client_role = m_config.client_role;
                replay.stability_ticks =
                    m_config.replay_fork_stability_ticks;
                replay.run_frames = m_config.replay_fork_run_frames;
                replay.require_rollback =
                    m_config.replay_fork_require_rollback;
                RollbackReplayForkRuntime::instance().configure(
                    std::move(replay));
            }
            else
            {
                RollbackProductionRuntime::instance().shutdown();
                RollbackReplayForkRuntime::instance().shutdown(true);
            }
            // The cleanup-only P2P driver is intentionally preserved across
            // this handoff. Its retry deadlines use this counter, so resetting
            // it here can turn a three-tick state settle into a multi-minute
            // wait while an unfocused client counts back up.
            if (!continuing_cleanup_handoff)
                m_service_ticks.store(0, std::memory_order_release);
            m_production_lobby_retry_tick = 0;
            m_production_non_pvp_observations = 0;
            m_cleanup_handoff_active = false;
            m_production_fail_closed_battle_manager = 0;
            m_production_last_trace_tick = 0;
            m_production_last_trace_state =
                RollbackProductionState::Disabled;
            m_production_last_organic_simulation_entries = UINT64_MAX;
            m_production_missing_organic_simulation_ticks = 0;
            m_snapshot_probe_ran = false;
            m_hgcpu_probe_ran = false;
            m_resim_probe_ran = false;
            m_cache_probe_ran = false;
            m_online_session_probe_ran = false;
            m_live_transport_probe_ran = false;
            m_live_peer_pipeline_probe_ran = false;
            m_end_to_end_probe_ran = false;
            m_live_activation_probe_ran = false;
            m_live_activation_executor_probe_ran = false;
            m_gekko_gameplay_input_probe_ran = false;
            m_gekko_session_probe_ran = false;
            m_gekko_adapter_probe_ran = false;
            m_gekko_udp_probe_ran = false;
            m_last_stock_transport_observe_total = UINT64_MAX;
            m_last_live_online_capture_total = UINT64_MAX;
            m_live_boundary_probe_ran = false;
            m_cache_injection_probe_ran = false;
            m_last_snapshot_probe = {};
            m_last_hgcpu_probe = {};
            m_last_resim_probe = {};
            m_last_cache_probe = {};
            m_last_online_session_probe = {};
            m_last_live_transport_probe = {};
            m_last_live_peer_pipeline_probe = {};
            m_last_end_to_end_probe = {};
            m_last_live_activation_probe = {};
            m_last_live_activation_executor_probe = {};
            m_last_gekko_gameplay_input_probe = {};
            m_last_gekko_session_probe = {};
            m_last_gekko_adapter_probe = {};
            m_last_gekko_udp_probe = {};
            m_last_stock_transport_observe_probe = {};
            m_last_live_online_capture_probe = {};
            m_last_live_activation_candidate = {};
            m_last_live_boundary_probe = {};
            m_last_cache_injection_probe = {};
            m_last_sidecar_probe = {};
            m_last_sidecar_packets_sent = 0;
            m_last_sidecar_packets_received = 0;
            m_last_sidecar_packets_rejected = 0;
            m_last_sidecar_direct_packets_received = 0;
            m_last_sidecar_direct_packets_rejected = 0;
            m_last_sidecar_ok = false;
            m_last_sidecar_validated_peer = false;
            m_last_sidecar_validated_direct_input = false;
            m_last_sidecar_validated_stock_offer = false;
            m_last_sidecar_remote_stock_fallback_ready = false;
            m_last_sidecar_sendto_error = 0;
            m_last_sidecar_recvfrom_error = 0;
            m_last_sidecar_had_packet_errors = false;
            m_sidecar_bind_emitted = false;
            m_sidecar_handshake_emitted = false;
            m_sidecar_handshake_ok_emitted = false;
            m_live_correction_probe_ran = false;
            m_live_disarm_emitted = false;
            m_sidecar.configure(
                m_config.enabled
                    && (m_config.test_case == RollbackLabCase::LiveOnlineCapture
                        || stock_online_production_requested())
                    && m_config.sidecar_requested(),
                m_config.local_peer_id,
                m_config.remote_peer_id,
                m_config.live_activation_session_id,
                m_config.sidecar_local_port,
                m_config.sidecar_remote_port,
                m_config.sidecar_remote_addr,
                m_config.activation_token);
            configure_p2p_harness();
            emit_qualification_activation();
            m_configured.store(true, std::memory_order_release);
            RollbackDiag::emit_configured(m_config, &m_manifest);
            RollbackDiag::emit_two_client_role_manifest(
                m_config, &m_manifest);
            if (m_config.enabled
                && (m_config.test_case == RollbackLabCase::OnlineBoundaryTrace
                    || m_config.test_case == RollbackLabCase::LiveOnlineCapture))
            {
                (void)RollbackLiveBoundaryHook::instance().install();
                RollbackLiveBoundaryHook::instance().begin_trace();
            }
            else
            {
                RollbackLiveBoundaryHook::instance().end_trace();
            }
            if (m_config.enabled
                && (m_config.test_case == RollbackLabCase::CacheInjectionTrace
                    || m_config.test_case
                        == RollbackLabCase::CachePredictionTrace))
            {
                (void)RollbackLiveBoundaryHook::instance().install();
                begin_configured_cache_probe();
            }
            else
            {
                RollbackLiveBoundaryHook::instance()
                    .end_cache_injection_probe();
            }
            if (m_config.test_case == RollbackLabCase::OnlineSessionSelfTest)
            {
                m_online_session_probe_ran = true;
                m_last_online_session_probe =
                    RunRollbackOnlineSessionSelfTest();
                RollbackDiag::emit_online_session_selftest(
                    m_last_online_session_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::LiveTransportSelfTest)
            {
                m_live_transport_probe_ran = true;
                m_last_live_transport_probe =
                    RunRollbackLiveTransportQueueSelfTest();
                RollbackDiag::emit_live_transport_selftest(
                    m_last_live_transport_probe, m_config);
            }
            if (m_config.test_case
                == RollbackLabCase::LivePeerPipelineSelfTest)
            {
                m_live_peer_pipeline_probe_ran = true;
                m_last_live_peer_pipeline_probe =
                    RunRollbackLivePeerPipelineSelfTest();
                RollbackDiag::emit_live_peer_pipeline_selftest(
                    m_last_live_peer_pipeline_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::EndToEndSelfTest)
            {
                m_end_to_end_probe_ran = true;
                m_last_end_to_end_probe = RunRollbackEndToEndSelfTest();
                RollbackDiag::emit_end_to_end_selftest(
                    m_last_end_to_end_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::LiveActivationSelfTest)
            {
                m_live_activation_probe_ran = true;
                m_last_live_activation_probe =
                    RunRollbackLiveActivationSelfTest();
                RollbackDiag::emit_live_activation_selftest(
                    m_last_live_activation_probe, m_config);
            }
            if (m_config.test_case
                == RollbackLabCase::LiveActivationExecutorSelfTest)
            {
                m_live_activation_executor_probe_ran = true;
                m_last_live_activation_executor_probe =
                    RunRollbackLiveActivationExecutorSelfTest();
                RollbackDiag::emit_live_activation_executor_selftest(
                    m_last_live_activation_executor_probe, m_config);
            }
            if (m_config.test_case
                == RollbackLabCase::GekkoGameplayInputSelfTest)
            {
                m_gekko_gameplay_input_probe_ran = true;
                m_last_gekko_gameplay_input_probe =
                    RunRollbackGekkoGameplayInputBridgeSelfTest();
                RollbackDiag::emit_gekko_gameplay_input_selftest(
                    m_last_gekko_gameplay_input_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::GekkoSessionSelfTest)
            {
                m_gekko_session_probe_ran = true;
                m_last_gekko_session_probe =
                    RunRollbackGekkoSessionSelfTest();
                RollbackDiag::emit_gekko_session_selftest(
                    m_last_gekko_session_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::GekkoAdapterSelfTest)
            {
                m_gekko_adapter_probe_ran = true;
                m_last_gekko_adapter_probe =
                    RunRollbackGekkoAdapterSelfTest();
                RollbackDiag::emit_gekko_adapter_selftest(
                    m_last_gekko_adapter_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::GekkoUdpSelfTest)
            {
                m_gekko_udp_probe_ran = true;
                m_last_gekko_udp_probe =
                    RunRollbackGekkoUdpAdapterSelfTest();
                RollbackDiag::emit_gekko_udp_selftest(
                    m_last_gekko_udp_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::StockTransportSelfTest)
            {
                m_stock_transport_probe_ran = true;
                m_last_stock_transport_probe =
                    RunRollbackStockTransportSurfaceSelfTest();
                RollbackDiag::emit_stock_transport_selftest(
                    m_last_stock_transport_probe, m_config);
            }
            if (m_config.enabled
                && (m_config.test_case == RollbackLabCase::StockTransportObserve
                    || m_config.test_case == RollbackLabCase::LiveOnlineCapture))
            {
                if (RollbackStockTransportObserveHook::instance().install())
                {
                    RollbackStockTransportObserveHook::instance().begin_trace();
                    m_last_stock_transport_observe_probe =
                        RollbackStockTransportObserveHook::instance().report();
                    m_last_stock_transport_observe_total =
                        m_last_stock_transport_observe_probe
                            .total_observed_calls;
                    RollbackDiag::emit_stock_transport_observe(
                        m_last_stock_transport_observe_probe, m_config);
                }
            }
            else
            {
                RollbackStockTransportObserveHook::instance().end_trace();
            }
            if (m_config.enabled
                && m_config.test_case == RollbackLabCase::LiveOnlineCapture)
            {
                if (NativeBinding::imageBase() != 0)
                {
                    m_last_live_boundary_probe =
                        RollbackLiveBoundaryHook::instance().report();
                    m_last_live_online_capture_probe =
                        EvaluateRollbackLiveOnlineCapture(
                            m_last_stock_transport_observe_probe,
                            m_last_live_boundary_probe);
                    m_last_live_activation_candidate =
                        evaluate_live_activation_candidate();
                    m_last_live_online_capture_total =
                        m_last_live_online_capture_probe.total_observed_calls;
                    RollbackDiag::emit_live_online_capture(
                        m_last_live_online_capture_probe, m_config);
                    RollbackDiag::emit_live_activation_candidate(
                        m_last_live_activation_candidate, m_config);
                }
            }
            }
            catch (...)
            {
                shutdown();
                production.reject_configuration(
                    "controller-config-allocation-failed");
            }
        }

        void shutdown() noexcept
        {
            emit_qualification_terminal();
            RollbackProductionRuntime::instance().shutdown();
            RollbackReplayForkRuntime::instance().shutdown(true);
            m_pending_config.reset();
            m_config.enabled = false;
            m_production_lobby_retry_tick = 0;
            m_production_non_pvp_observations = 0;
            m_cleanup_handoff_active = false;
            m_production_fail_closed_battle_manager = 0;
            m_configured.store(false, std::memory_order_release);
            m_history.clear();
            m_snapshot.clear();
            m_hgcpu_snapshot.clear();
            m_snapshot_probe_ran = false;
            m_hgcpu_probe_ran = false;
            m_resim_probe_ran = false;
            m_cache_probe_ran = false;
            m_online_session_probe_ran = false;
            m_live_transport_probe_ran = false;
            m_live_peer_pipeline_probe_ran = false;
            m_end_to_end_probe_ran = false;
            m_live_activation_probe_ran = false;
            m_live_activation_executor_probe_ran = false;
            m_gekko_session_probe_ran = false;
            m_gekko_gameplay_input_probe_ran = false;
            m_gekko_adapter_probe_ran = false;
            m_gekko_udp_probe_ran = false;
            m_stock_transport_probe_ran = false;
            m_last_stock_transport_observe_total = UINT64_MAX;
            m_last_live_online_capture_total = UINT64_MAX;
            m_live_boundary_probe_ran = false;
            m_cache_injection_probe_ran = false;
            m_last_snapshot_probe = {};
            m_last_hgcpu_probe = {};
            m_last_resim_probe = {};
            m_last_cache_probe = {};
            m_last_online_session_probe = {};
            m_last_live_transport_probe = {};
            m_last_live_peer_pipeline_probe = {};
            m_last_end_to_end_probe = {};
            m_last_live_activation_probe = {};
            m_last_live_activation_executor_probe = {};
            m_last_gekko_session_probe = {};
            m_last_gekko_gameplay_input_probe = {};
            m_last_gekko_adapter_probe = {};
            m_last_gekko_udp_probe = {};
            m_last_stock_transport_probe = {};
            m_last_stock_transport_observe_probe = {};
            m_last_live_online_capture_probe = {};
            m_last_live_activation_candidate = {};
            m_last_live_boundary_probe = {};
            m_last_cache_injection_probe = {};
            m_last_sidecar_probe = {};
            m_sidecar.close();
            m_p2p_harness.shutdown();
            m_last_sidecar_packets_sent = 0;
            m_last_sidecar_packets_received = 0;
            m_last_sidecar_packets_rejected = 0;
            m_last_sidecar_direct_packets_received = 0;
            m_last_sidecar_direct_packets_rejected = 0;
            m_last_sidecar_ok = false;
            m_last_sidecar_validated_peer = false;
            m_last_sidecar_validated_direct_input = false;
            m_last_sidecar_sendto_error = 0;
            m_last_sidecar_recvfrom_error = 0;
            m_last_sidecar_had_packet_errors = false;
            m_sidecar_bind_emitted = false;
            m_sidecar_handshake_emitted = false;
            m_sidecar_handshake_ok_emitted = false;
            m_live_correction_probe_ran = false;
            m_live_disarm_emitted = false;
            RollbackLiveBoundaryHook::instance().end_trace();
            RollbackLiveBoundaryHook::instance().end_cache_injection_probe();
            RollbackStockTransportObserveHook::instance().end_trace();
        }

        void append_qualification_tags(ReplayTraceFields& fields) const
            noexcept
        {
            fields
                .uinteger("qualification_contract_version",
                    m_config.qualification_contract_version)
                .string("qualification_run_id",
                    m_config.qualification_run_id)
                .string("qualification_case_id",
                    m_config.qualification_case_id)
                .string("qualification_segment_id",
                    m_config.qualification_segment_id)
                .string("qualification_schedule_hash",
                    m_config.qualification_schedule_hash)
                .string("runtime_profile", RollbackNetworkProfileName(
                    m_config.production.network_profile))
                .uinteger("qualification_seed",
                    m_config.production.fault_seed)
                .uinteger("protocol_version",
                    m_config.qualification_protocol_version)
                .uinteger("snapshot_version",
                    m_config.qualification_snapshot_version)
                .string("qualification_role", m_config.client_role);
        }

        void emit_qualification_activation() noexcept
        {
            if (!m_config.qualification_enabled()) return;
            LARGE_INTEGER frequency {};
            QueryPerformanceFrequency(&frequency);
            ReplayTraceFields fields;
            append_qualification_tags(fields);
            fields
                .uinteger("qpc_frequency",
                    static_cast<uint64_t>(frequency.QuadPart))
                .uinteger("confirmed_frames_total", 0);
            ReplayDebugTrace::instance().event(
                "rollback_qualification_activation", fields);
            m_qualification_active = true;
            m_qualification_confirmed_frames_total = 0;
            m_qualification_completed_confirmed_frames = 0;
            m_qualification_last_round_generation = UINT32_MAX;
            m_qualification_last_confirmed_frame = -1;
        }

        void observe_qualification_status(
            const RollbackProductionStatus& status) noexcept
        {
            if (!m_qualification_active || !status.confirmed_frame.valid)
                return;
            const int32_t confirmed = static_cast<int32_t>(
                status.confirmed_frame.value);
            if (m_qualification_last_round_generation == UINT32_MAX)
            {
                m_qualification_last_round_generation =
                    status.round_generation;
            }
            else if (status.round_generation !=
                    m_qualification_last_round_generation)
            {
                m_qualification_completed_confirmed_frames =
                    m_qualification_confirmed_frames_total;
                m_qualification_last_round_generation =
                    status.round_generation;
                m_qualification_last_confirmed_frame = -1;
            }
            if (confirmed >= m_qualification_last_confirmed_frame)
            {
                m_qualification_last_confirmed_frame = confirmed;
                m_qualification_confirmed_frames_total =
                    m_qualification_completed_confirmed_frames
                    + static_cast<uint64_t>(confirmed) + 1;
            }
        }

        void emit_qualification_terminal() noexcept
        {
            if (!m_qualification_active) return;
            RollbackProductionRuntime& production =
                RollbackProductionRuntime::instance();
            const RollbackProductionStatus& status = production.status();
            observe_qualification_status(status);
            std::array<uint8_t, 32> source_replay_digest {};
            std::string source_replay_sha256;
            if (production.qualification_source_replay_sha256(
                    source_replay_digest))
            {
                static constexpr char kHex[] = "0123456789abcdef";
                source_replay_sha256.resize(source_replay_digest.size() * 2);
                for (size_t i = 0; i < source_replay_digest.size(); ++i)
                {
                    source_replay_sha256[i * 2] =
                        kHex[source_replay_digest[i] >> 4];
                    source_replay_sha256[i * 2 + 1] =
                        kHex[source_replay_digest[i] & 0x0F];
                }
            }
            ReplayTraceFields fields;
            append_qualification_tags(fields);
            fields
                .uinteger("confirmed_frames_total",
                    m_qualification_confirmed_frames_total)
                .boolean("clean_shutdown",
                    status.state != RollbackProductionState::Fatal)
                .boolean("fatal_failure",
                    status.state == RollbackProductionState::Fatal)
                .string("terminal_failure",
                    status.failure ? status.failure : "unknown")
                .uinteger("canonical_mismatches",
                    status.stock_round_terminal_candidate_mismatches)
                .boolean("fail_closed",
                    status.state == RollbackProductionState::Fatal
                    || status.network_failure != 0)
                .boolean("clean_lobby_recovery",
                    status.lobby_return_succeeded)
                .string("content_sha256", source_replay_sha256)
                .hex("steam_lobby_id", status.steam_lobby_id)
                .hex("session_contract_hash", status.session_contract_hash)
                .hex("steam_identity_accepted_selection_hash",
                    status.steam_identity_accepted_selection_hash)
                .hex("launch_stage_identity",
                    status.launch_stage_identity)
                .hex("session_epoch", status.session_epoch)
                .hex("steam_local_id", status.steam_local_id)
                .hex("steam_remote_id", status.steam_remote_id)
                .hex("steam_owner_id", status.steam_owner_id)
                .uinteger("local_player_slot",
                    status.local_player_slot)
                .uinteger("service_tick",
                    m_service_ticks.load(std::memory_order_acquire))
                .uinteger("round_generation", status.round_generation)
                .uinteger("fault_submitted",
                    status.fault_packets_submitted)
                .uinteger("fault_queued", status.fault_packets_queued)
                .uinteger("fault_delivered", status.fault_packets_delivered)
                .uinteger("fault_dropped", status.fault_packets_dropped)
                .uinteger("fault_duplicated",
                    status.fault_packets_duplicated)
                .uinteger("fault_reordered",
                    status.fault_packets_reordered)
                .uinteger("fault_corrupted",
                    status.fault_packets_corrupted)
                .uinteger("fault_spiked", status.fault_packets_spiked)
                .uinteger("fault_burst_dropped",
                    status.fault_packets_burst_dropped)
                .uinteger("test_worker_stalls_started",
                    status.test_worker_stalls_started)
                .uinteger("test_worker_stalls_completed",
                    status.test_worker_stalls_completed)
                .uinteger("test_worker_stall_actual_ms",
                    status.test_worker_stall_actual_ms)
                .uinteger("network_packets_sent",
                    status.network_packets_sent)
                .uinteger("network_packets_received",
                    status.network_packets_received)
                .uinteger("network_packets_authenticated",
                    status.network_packets_authenticated)
                .uinteger("network_packets_rejected",
                    status.network_packets_rejected)
                .uinteger("network_packets_decode_rejected",
                    status.network_packets_decode_rejected)
                .uinteger("network_packets_route_rejected",
                    status.network_packets_route_rejected)
                .uinteger("network_packets_replay_rejected",
                    status.network_packets_replay_rejected)
                .uinteger("stock_round_terminal_candidate_matches",
                    status.stock_round_terminal_candidate_matches)
                .uinteger("steam_route_current_rtt_us",
                    status.steam_route_statistics.current_rtt_us)
                .uinteger("steam_route_jitter_us",
                    status.steam_route_statistics.jitter_us);
            ReplayDebugTrace::instance().event(
                "rollback_qualification_terminal", fields);
            m_qualification_active = false;
            ReplayDebugTrace::instance().close_session();
        }

        RollbackModuleUnloadResult prepare_for_module_unload() noexcept
        {
            const RollbackModuleUnloadResult result =
                RollbackProductionRuntime::instance()
                    .prepare_for_module_unload();
            if (result != RollbackModuleUnloadResult::Ready)
                return result;
            shutdown();
            return RollbackModuleUnloadResult::Ready;
        }

        bool configured() const noexcept
        {
            return m_configured.load(std::memory_order_acquire);
        }

        bool enabled() const noexcept
        {
            return configured() && m_config.enabled;
        }

        const RollbackLabConfig& config() const noexcept
        {
            return m_config;
        }

        const RollbackSnapshotManifest& manifest() const noexcept
        {
            return m_manifest;
        }

        uint64_t service_ticks() const noexcept
        {
            return m_service_ticks.load(std::memory_order_acquire);
        }

        void set_enabled_from_ui(bool enabled) noexcept
        {
            try
            {
            RollbackLabConfig cfg {};
            if (configured())
                cfg = m_config;
            cfg.enabled = enabled;
            cfg.source = "ui";
            if (cfg.test_case == RollbackLabCase::Production)
                cfg.production.enabled = enabled;
            configure(std::move(cfg));
            }
            catch (...)
            {
                reject_configuration_failure("ui-config-allocation-failed");
            }
        }

        void reject_configuration_failure(const char* failure) noexcept
        {
            auto& production = RollbackProductionRuntime::instance();
            if (production.owns_tick_boundary())
            {
                production.request_fail_closed(
                    failure ? failure : "controller-configuration-failed");
                return;
            }
            shutdown();
            production.reject_configuration(
                failure ? failure : "controller-configuration-failed");
        }

        void service_game_thread() noexcept
        {
            try
            {
            if (!enabled()) return;
            refresh_manifest_image_base_if_needed();
            const uint64_t tick =
                m_service_ticks.fetch_add(1, std::memory_order_acq_rel) + 1;
            auto& production = RollbackProductionRuntime::instance();
            // Stock-online boundary observation performs its own immediate
            // validation at frame zero. The full lifecycle capture also
            // hashes the breakable-stage actor set, so polling it every game
            // tick needlessly starves two local clients while they wait.
            const bool stock_online_local_lab =
                stock_online_local_lab_requested();
            const bool stock_online_production =
                stock_online_production_requested();
            // Stock rollback captures its full immutable identity inside the
            // native tick observer at a real frame-zero boundary. UObject/
            // stage discovery on this game-flow service thread races battle
            // construction. Setup-only also has no rollback identity work.
            if (!stock_online_production)
                refresh_manifest_lifecycle_if_ready();
            if (m_manifest.epoch.valid
                && m_manifest.epoch.battle_manager != 0)
            {
                // Emergency transition context only. This last-known-valid
                // pointer is never accepted for snapshot or simulation work;
                // it survives epoch loss solely so fail-closed can still ask
                // the native manager to leave the round.
                m_production_fail_closed_battle_manager =
                    m_manifest.epoch.battle_manager;
            }
            if (tick == 1 || (tick % 600) == 0)
                RollbackDiag::emit_service_tick(tick, m_config);
            if (m_config.test_case == RollbackLabCase::Production)
            {
                if (stock_online_production)
                {
                    // Terminal callbacks are a lightweight cross-thread
                    // handoff and must continue after the local qualification
                    // lane stops the heavy UObject/setup observer at Active.
                    // Consume them before publishing the current identity so
                    // production revokes on this same game-thread service.
                    m_p2p_harness
                        .service_connection_terminal_events_game_thread(tick);
                    production.set_steam_session_identity(
                        m_p2p_harness.stock_steam_session_identity());
                    if (!stock_online_local_lab)
                    {
                        m_p2p_harness.service_game_thread(tick);
                        if (tick == 1 || (tick % 6) == 0)
                        {
                            sync_stock_invite_sidecar();
                            service_sidecar_live_proof(tick);
                        }
                        production.set_steam_session_identity(
                            m_p2p_harness.stock_steam_session_identity());
                        if (!m_manifest.epoch.active_for(
                                RollbackLifecycleMode::StockOnlinePvp)
                            && (tick % 6) == 0)
                        {
                            refresh_manifest_lifecycle_if_ready();
                        }
                    }
                    if (m_cleanup_handoff_active)
                        m_p2p_harness.service_game_thread(tick);
                    // The stock driver owns setup only. Once the native tick
                    // hook owns simulation, stop all UObject discovery,
                    // invite polling, and detailed setup tracing.
                    if (stock_online_local_lab
                        && production.status().state
                            != RollbackProductionState::Active
                        && production.status().state
                            != RollbackProductionState::
                                WaitingForRoundTransition)
                    {
                        const bool setup_battle_ready =
                            m_p2p_harness
                                .stock_battle_construction_ready();
                        const bool service_stock_driver =
                            ShouldServiceStockBattleControlPlane(
                                setup_battle_ready, tick);
                        // Keep UI observation per-tick: startup scene changes
                        // are brief. The authenticated setup sidecar is only
                        // a control channel, so 10 Hz is sufficient and avoids
                        // doing socket/offer work 60 times per second.
                        const bool service_setup_sidecar =
                            tick == 1 || (tick % 6) == 0;
                        if (service_setup_sidecar)
                            sync_stock_invite_sidecar();
                        if (service_stock_driver)
                            m_p2p_harness.service_game_thread(tick);
                        production.set_steam_session_identity(
                            m_p2p_harness.stock_steam_session_identity());
                        const bool stock_identity_active =
                            m_manifest.epoch.active_for(
                                RollbackLifecycleMode::StockOnlinePvp);
                        if (!stock_identity_active
                            && (m_config.production.replay_input.enabled
                                || (tick % 60) == 0)
                            && m_p2p_harness
                                .stock_battle_construction_ready())
                        {
                            // Battle construction is complete. Discover the
                            // one online-active manager on the game thread,
                            // once, before arming the native observer.
                            refresh_manifest_lifecycle_if_ready();
                            if (!m_manifest.epoch.active_for(
                                    RollbackLifecycleMode::StockOnlinePvp))
                            {
                                const auto& discovery =
                                    RollbackOnlineBattleManagerDiscoveryState();
                                ReplayTraceFields fields;
                                fields.string("request_id", m_config.request_id)
                                    .string("client_role", m_config.client_role)
                                    .uinteger("class_candidates",
                                        discovery.class_candidates)
                                    .uinteger("input_log_candidates",
                                        discovery.input_log_candidates)
                                    .uinteger("native_slot_candidates",
                                        discovery.native_slot_candidates)
                                    .uinteger("stage_candidates",
                                        discovery.stage_candidates)
                                    .uinteger("fighter_candidates",
                                        discovery.fighter_candidates)
                                    .integer("last_active_slot_count",
                                        discovery.last_active_slot_count)
                                    .hex("last_active_slot_mask",
                                        discovery.last_active_slot_mask)
                                    .hex("last_candidate",
                                        discovery.last_candidate)
                                    .hex("last_input_log",
                                        discovery.last_input_log);
                                ReplayDebugTrace::instance().event(
                                    "rollback_stock_identity_discovery",
                                    fields);
                            }
                        }
                        if (service_setup_sidecar)
                        {
                            sync_stock_invite_sidecar();
                            service_sidecar_live_proof(tick);
                        }
                    }
                }
                const bool production_active = production.status().state
                    == RollbackProductionState::Active;
                if (production_active)
                {
                    // Gekko may stop reaching the owned native tick while
                    // blocked on a lost peer. This watchdog is deliberately
                    // transport/lifecycle-only and does not service rollback
                    // simulation from the outer game-thread callback.
                    production.service_active_peer_liveness_watchdog();
                    const uint64_t organic_entries = production.status()
                        .owned_simulation_organic_entries;
                    if (organic_entries
                        != m_production_last_organic_simulation_entries)
                    {
                        m_production_last_organic_simulation_entries =
                            organic_entries;
                        m_production_missing_organic_simulation_ticks = 0;
                    }
                    else if (m_production_missing_organic_simulation_ticks
                        < UINT32_MAX)
                    {
                        ++m_production_missing_organic_simulation_ticks;
                    }
                    if (ShouldProbeActiveOwnedSimulationLiveness(
                            true, organic_entries,
                            m_production_last_organic_simulation_entries,
                            m_production_missing_organic_simulation_ticks))
                    {
                        production.service_active_owned_simulation_liveness();
                    }
                }
                else
                {
                    m_production_last_organic_simulation_entries = UINT64_MAX;
                    m_production_missing_organic_simulation_ticks = 0;
                }
                const bool round_transition_waiting =
                    production.status().state
                        == RollbackProductionState::WaitingForRoundTransition;
                // A stock match keeps its accepted BattleManager, InputLog,
                // fighters and stage objects between rounds.  The native
                // PerFrame detour is the only authority allowed to observe and
                // freeze the next-round boundary; refreshing UObject lifecycle
                // state here raced that hook and let one peer miss frame zero.
                const bool stock_battle_ready = !stock_online_production
                    || m_p2p_harness
                        .stock_battle_construction_ready();
                const bool production_service_due = round_transition_waiting
                    || ShouldServiceStockBattleControlPlane(
                        stock_battle_ready, tick);
                if (production_service_due
                    && ShouldServiceRollbackProduction(
                        true, production_active,
                        production.status()
                            .native_round_over_commit_observation_pending))
                {
                    production.service_game_thread(
                        m_manifest, stock_battle_ready);
                }
                if (production.status().state
                        == RollbackProductionState::Fatal
                    && production.owns_tick_boundary()
                    && !m_cleanup_handoff_active)
                {
                    try
                    {
                        RollbackLabConfig cleanup = m_config;
                        cleanup.enabled = true;
                        cleanup.test_case = RollbackLabCase::Production;
                        cleanup.online_stage_requested = true;
                        cleanup.online_stage_cleanup_only = true;
                        cleanup.stock_join_route =
                            RollbackStockJoinRoute::InjectedStockInvite;
                        m_pending_config = cleanup;
                        (void)arm_cleanup_handoff(*m_pending_config);
                    }
                    catch (...)
                    {
                        production.request_fail_closed(
                            "cleanup-handoff-allocation-failed");
                    }
                }
                production.sync_native_hook_lifetime_status();
                const RollbackProductionStatus& status = production.status();
                if (tick == 1 || status.state !=
                        m_production_last_trace_state ||
                    tick - m_production_last_trace_tick >= 60)
                {
                    ReplayTraceFields fields;
                    fields.string("request_id", m_config.request_id)
                        .string("client_role", m_config.client_role)
                        .string("lifecycle_mode", RollbackLifecycleModeName(
                            m_config.production.lifecycle_mode))
                        .uinteger("service_tick", tick)
                        .uinteger("state",
                            static_cast<uint8_t>(status.state))
                        .string("state_name",
                            RollbackProductionStateName(status.state))
                        .string("failure", status.failure
                            ? status.failure : "unknown")
                        .boolean("executable_match", status.executable_match)
                        .boolean("schema_match", status.schema_match)
                        .hex("executable_id", status.executable_id)
                        .hex("schema_id", status.schema_id)
                        .boolean("manifest_ready", status.manifest_ready)
                        .boolean("lifecycle_ready", status.lifecycle_ready)
                        .boolean("peer_ready", status.peer_ready)
                        .string("selected_route",
                            RollbackRouteKindName(status.selected_route))
                        .string("route_switch_reason",
                            RollbackRouteSwitchReasonName(
                                status.route_switch_reason))
                        .uinteger("route_decision_count",
                            status.route_decision_count)
                        .boolean("direct_route_available",
                            status.direct_route_statistics.available)
                        .boolean("direct_route_healthy",
                            status.direct_route_statistics.healthy)
                        .uinteger("direct_route_samples",
                            status.direct_route_statistics.rolling_samples)
                        .uinteger("direct_route_current_rtt_us",
                            status.direct_route_statistics.current_rtt_us)
                        .uinteger("direct_route_median_rtt_us",
                            status.direct_route_statistics.median_rtt_us)
                        .uinteger("direct_route_p95_rtt_us",
                            status.direct_route_statistics.p95_rtt_us)
                        .uinteger("direct_route_p99_rtt_us",
                            status.direct_route_statistics.p99_rtt_us)
                        .uinteger("direct_route_jitter_us",
                            status.direct_route_statistics.jitter_us)
                        .uinteger("direct_route_loss_per_mille",
                            status.direct_route_statistics
                                .estimated_loss_per_mille)
                        .uinteger("direct_route_maximum_loss_burst",
                            status.direct_route_statistics
                                .maximum_loss_burst)
                        .uinteger("direct_route_probes_sent",
                            status.direct_route_statistics.probes_sent)
                        .uinteger("direct_route_probes_acknowledged",
                            status.direct_route_statistics
                                .probes_acknowledged)
                        .uinteger("direct_route_probes_lost",
                            status.direct_route_statistics.probes_lost)
                        .uinteger("direct_route_deadline_miss_per_mille",
                            status.direct_route_statistics
                                .deadline_miss_per_mille)
                        .uinteger("direct_route_deadline_window_samples",
                            status.direct_route_statistics
                                .deadline_window_samples)
                        .uinteger("direct_route_health_changes",
                            status.direct_route_statistics.health_changes)
                        .uinteger("direct_route_last_receive_us",
                            status.direct_route_statistics
                                .last_successful_receive_us)
                        .uinteger("direct_route_duplicates",
                            status.direct_route_statistics.duplicates)
                        .uinteger("direct_route_reordered",
                            status.direct_route_statistics.reordered)
                        .uinteger("direct_route_failure",
                            static_cast<uint8_t>(
                                status.direct_route_statistics
                                    .transport_failure))
                        .boolean("steam_route_available",
                            status.steam_route_statistics.available)
                        .boolean("steam_route_healthy",
                            status.steam_route_statistics.healthy)
                        .uinteger("steam_route_samples",
                            status.steam_route_statistics.rolling_samples)
                        .uinteger("steam_route_current_rtt_us",
                            status.steam_route_statistics.current_rtt_us)
                        .uinteger("steam_route_median_rtt_us",
                            status.steam_route_statistics.median_rtt_us)
                        .uinteger("steam_route_p95_rtt_us",
                            status.steam_route_statistics.p95_rtt_us)
                        .uinteger("steam_route_p99_rtt_us",
                            status.steam_route_statistics.p99_rtt_us)
                        .uinteger("steam_route_jitter_us",
                            status.steam_route_statistics.jitter_us)
                        .uinteger("steam_route_loss_per_mille",
                            status.steam_route_statistics
                                .estimated_loss_per_mille)
                        .uinteger("steam_route_maximum_loss_burst",
                            status.steam_route_statistics
                                .maximum_loss_burst)
                        .uinteger("steam_route_probes_sent",
                            status.steam_route_statistics.probes_sent)
                        .uinteger("steam_route_probes_acknowledged",
                            status.steam_route_statistics
                                .probes_acknowledged)
                        .uinteger("steam_route_probes_lost",
                            status.steam_route_statistics.probes_lost)
                        .uinteger("steam_route_deadline_miss_per_mille",
                            status.steam_route_statistics
                                .deadline_miss_per_mille)
                        .uinteger("steam_route_deadline_window_samples",
                            status.steam_route_statistics
                                .deadline_window_samples)
                        .uinteger("steam_route_health_changes",
                            status.steam_route_statistics.health_changes)
                        .uinteger("steam_route_last_receive_us",
                            status.steam_route_statistics
                                .last_successful_receive_us)
                        .uinteger("steam_route_duplicates",
                            status.steam_route_statistics.duplicates)
                        .uinteger("steam_route_reordered",
                            status.steam_route_statistics.reordered)
                        .uinteger("steam_route_failure",
                            static_cast<uint8_t>(
                                status.steam_route_statistics
                                    .transport_failure))
                        .boolean("steam_session_valid",
                            status.steam_session_valid)
                        .uinteger("steam_session_lifecycle",
                            static_cast<uint8_t>(
                                status.steam_session_lifecycle))
                        .uinteger("steam_bootstrap_attempt",
                            status.steam_bootstrap_attempt)
                        .uinteger("steam_bootstrap_attempt_limit",
                            status.steam_bootstrap_attempt_limit)
                        .boolean("steam_bootstrap_retry_exhausted",
                            status.steam_bootstrap_retry_exhausted)
                        .uinteger("steam_transport_lifecycle",
                            static_cast<uint8_t>(
                                status.steam_transport_lifecycle))
                        .uinteger("steam_bootstrap_final_failure",
                            static_cast<uint8_t>(
                                status.steam_bootstrap_final_failure))
                        .hex("steam_native_epoch_key",
                            status.steam_native_epoch_key)
                        .boolean("steam_key_confirmed",
                            status.steam_key_confirmed)
                        .boolean("steam_using_relay",
                            status.steam_using_relay)
                        .uinteger("steam_interface_revision",
                            status.steam_interface_revision)
                        .boolean("session_contract_local",
                            status.session_contract_local)
                        .boolean("session_contract_peer",
                            status.session_contract_peer)
                        .boolean("session_contract_ready",
                            status.session_contract_ready)
                        .uinteger("session_contract_local_stage",
                            status.session_contract_local_stage)
                        .uinteger("session_contract_peer_stage",
                            status.session_contract_peer_stage)
                        .hex("steam_lobby_id", status.steam_lobby_id)
                        .hex("steam_owner_id", status.steam_owner_id)
                        .hex("steam_local_id", status.steam_local_id)
                        .hex("steam_remote_id", status.steam_remote_id)
                        .hex("steam_identity_conflict_mask",
                            status.steam_identity_conflict_mask)
                        .hex("steam_stable_identity_conflict_mask",
                            status.steam_stable_identity_conflict_mask)
                        .hex("steam_native_epoch_conflict_mask",
                            status.steam_native_epoch_conflict_mask)
                        .uinteger(
                            "steam_identity_accepted_lifecycle_serial",
                            status
                                .steam_identity_accepted_lifecycle_serial)
                        .hex("steam_identity_accepted_selection_hash",
                            status.steam_identity_accepted_selection_hash)
                        .hex("steam_identity_observed_lobby_id",
                            status.steam_identity_observed_lobby_id)
                        .hex("steam_identity_observed_owner_id",
                            status.steam_identity_observed_owner_id)
                        .hex("steam_identity_observed_local_id",
                            status.steam_identity_observed_local_id)
                        .hex("steam_identity_observed_remote_id",
                            status.steam_identity_observed_remote_id)
                        .hex("steam_identity_observed_selection_hash",
                            status.steam_identity_observed_selection_hash)
                        .uinteger(
                            "steam_identity_observed_lifecycle_serial",
                            status
                                .steam_identity_observed_lifecycle_serial)
                        .hex("steam_identity_observed_epoch_key",
                            status.steam_identity_observed_epoch_key)
                        .uinteger(
                            "steam_identity_observed_connect_state",
                            status.steam_identity_observed_connect_state)
                        .uinteger(
                            "steam_identity_observed_connect_sub_state",
                            status
                                .steam_identity_observed_connect_sub_state)
                        .uinteger("steam_terminal_evidence",
                            static_cast<uint8_t>(
                                status.steam_terminal_evidence))
                        .hex("session_contract_hash",
                            status.session_contract_hash)
                        .hex("session_epoch", status.session_epoch)
                        .uinteger("session_contract_messages_sent",
                            status.session_contract_messages_sent)
                        .uinteger("session_contract_messages_received",
                            status.session_contract_messages_received)
                        .boolean("launch_baseline_local",
                            status.launch_baseline_local)
                        .boolean("launch_baseline_peer",
                            status.launch_baseline_peer)
                        .boolean("launch_barrier_ready",
                            status.launch_barrier_ready)
                        .boolean("launch_baseline_stable",
                            status.launch_baseline_stable)
                        .hex("launch_baseline_hash",
                            status.launch_baseline_hash)
                        .hex("peer_launch_baseline_hash",
                            status.peer_launch_baseline_hash)
                        .hex("launch_baseline_hgcpu_hash",
                            status.launch_baseline_hgcpu_hash)
                        .hex("launch_baseline_explicit_hash",
                            status.launch_baseline_explicit_hash)
                        .hex("launch_baseline_stage_hash",
                            status.launch_baseline_stage_hash)
                        .hex("launch_baseline_wind_hash",
                            status.launch_baseline_wind_hash)
                        .hex("launch_baseline_input_0",
                            status.launch_baseline_input_0)
                        .hex("launch_baseline_input_1",
                            status.launch_baseline_input_1)
                        .hex("launch_baseline_epoch",
                            status.launch_baseline_epoch)
                        .hex("peer_launch_baseline_epoch",
                            status.peer_launch_baseline_epoch)
                        .hex("launch_stage_identity",
                            status.launch_stage_identity)
                        .hex("peer_launch_stage_identity",
                            status.peer_launch_stage_identity)
                        .hex("launch_lifecycle_digest",
                            status.launch_lifecycle_digest)
                        .hex("peer_launch_lifecycle_digest",
                            status.peer_launch_lifecycle_digest)
                        .boolean("gekko_ready", status.gekko_ready)
                        .uinteger("native_input_source_slot",
                            status.native_input_source_slot)
                        .boolean("native_input_ownership_verified",
                            status.native_input_ownership_verified)
                        .integer("native_active_slot_count",
                            status.native_active_slot_count)
                        .uinteger("native_active_slot_mask",
                            status.native_active_slot_mask)
                        .uinteger("gekko_slot", status.local_player_slot)
                        .boolean("tick_hook_installed",
                            status.tick_hook_installed)
                        .boolean("input_consumer_validated",
                            status.input_consumer_validated)
                        .boolean("presentation_hooks_installed",
                            status.presentation_hooks_installed)
                        .boolean("presentation_terminal_dispatch_complete",
                            status.presentation_terminal_dispatch_complete)
                        .boolean("battle_event_hub_passthrough",
                            status.battle_event_hub_passthrough)
                        .boolean("audio_semantic_passthrough",
                            status.audio_semantic_passthrough)
                        .uinteger("presentation_hook_install_mask",
                            status.presentation_hook_install_mask)
                        .uinteger("presentation_chara_hook_install_mask",
                            status.presentation_chara_hook_install_mask)
                        .uinteger("vfx_slot_validation_stage",
                            status.vfx_slot_validation_stage)
                        .boolean("vfx_slot_target_is_static_noop",
                            status.vfx_slot_target_is_static_noop)
                        .boolean("vfx_slot_patch_installed",
                            status.vfx_slot_patch_installed)
                        .boolean("vfx_install_restart_required",
                            status.vfx_install_restart_required)
                        .hex("vfx_slot_patch_mask",
                            status.vfx_slot_patch_mask)
                        .uinteger("vfx_binding_failed_slot",
                            status.vfx_binding_failed_slot)
                        .hex("vfx_dispatcher_address",
                            status.vfx_dispatcher_address)
                        .hex("vfx_vtable_address",
                            status.vfx_vtable_address)
                        .hex("vfx_slot_target_address",
                            status.vfx_slot_target_address)
                        .hex("vfx_slot_target_rva",
                            status.vfx_slot_target_rva)
                        .uinteger("stock_observer_ticks",
                            status.stock_observer_ticks)
                        .uinteger("stock_observer_last_frame",
                            status.stock_observer_last_frame)
                        .uinteger("stock_observer_last_input_log_frame",
                            status.stock_observer_last_input_log_frame)
                        .uinteger("stock_boundary_arm_frame",
                            status.stock_boundary_arm_frame)
                        .uinteger("stock_boundary_freeze_frame",
                            status.stock_boundary_freeze_frame)
                        .uinteger("stock_observer_trampoline_calls",
                            status.stock_observer_trampoline_calls)
                        .uinteger("initial_boundary_probe_candidates",
                            status.initial_boundary_probe_candidates)
                        .uinteger("initial_boundary_commits",
                            status.initial_boundary_commits)
                        .uinteger("precontrol_candidates",
                            status.precontrol_candidates)
                        .uinteger("precontrol_observations",
                            status.precontrol_observations)
                        .uinteger("precontrol_not_due",
                            status.precontrol_not_due)
                        .uinteger("precontrol_wrong_mode",
                            status.precontrol_wrong_mode)
                        .uinteger("precontrol_queued_mode",
                            status.precontrol_queued_mode)
                        .uinteger("precontrol_unreadable",
                            status.precontrol_unreadable)
                        .uinteger("precontrol_holds",
                            status.precontrol_holds)
                        .uinteger("precontrol_release_failures",
                            status.precontrol_release_failures)
                        .boolean("precontrol_iteration_frozen",
                            status.precontrol_iteration_frozen)
                        .boolean("precontrol_shutdown_authorized",
                            status.precontrol_shutdown_authorized)
                        .uinteger("precontrol_frame_zero_executions",
                            status.precontrol_frame_zero_executions)
                        .uinteger("precontrol_shutdown_refusals",
                            status.precontrol_shutdown_refusals)
                        .uinteger("precontrol_status1_freezes",
                            status.precontrol_status1_freezes)
                        .uinteger("precontrol_mode_frame",
                            status.precontrol_mode_frame)
                        .uinteger("precontrol_phase_timer",
                            status.precontrol_phase_timer)
                        .uinteger("precontrol_identity_digest",
                            status.precontrol_identity_digest)
                        .uinteger(
                            "initial_boundary_frozen_clock_restore_attempts",
                            status.initial_boundary_frozen_clock_restore_attempts)
                        .uinteger(
                            "initial_boundary_frozen_clock_restores",
                            status.initial_boundary_frozen_clock_restores)
                        .uinteger(
                            "initial_boundary_frozen_clock_restore_failures",
                            status.initial_boundary_frozen_clock_restore_failures)
                        .uinteger("initial_boundary_prestart_captures",
                            status.initial_boundary_prestart_captures)
                        .uinteger("initial_boundary_prestart_restores",
                            status.initial_boundary_prestart_restores)
                        .uinteger("initial_boundary_prestart_verifications",
                            status.initial_boundary_prestart_verifications)
                        .boolean("initial_boundary_clock_captured",
                            status.initial_boundary_clock_captured)
                        .integer("initial_boundary_input_log_last_frame",
                            status.initial_boundary_input_log_last_frame)
                        .uinteger(
                            "initial_boundary_input_log_master_clock",
                            status.initial_boundary_input_log_master_clock)
                        .integer("initial_boundary_battle_last_frame",
                            status.initial_boundary_battle_last_frame)
                        .uinteger("initial_boundary_battle_last_applied",
                            status.initial_boundary_battle_last_applied)
                        .uinteger("initial_boundary_pending_delta",
                            status.initial_boundary_pending_delta)
                        .uinteger("stock_trampoline_calls_after_freeze",
                            status.stock_trampoline_calls_after_freeze)
                        .uinteger("baseline_frozen_microseconds",
                            status.baseline_frozen_microseconds)
                        .uinteger("launch_local_stage",
                            status.launch_local_stage)
                        .uinteger("launch_peer_stage",
                            status.launch_peer_stage)
                        .uinteger("launch_local_input_log_frame",
                            status.launch_local_input_log_frame)
                        .uinteger("launch_peer_input_log_frame",
                            status.launch_peer_input_log_frame)
                        .integer("launch_baseline_frame",
                            status.launch_baseline_frame)
                        .uinteger("launch_logical_frame",
                            status.launch_logical_frame)
                        .hex("launch_local_epoch",
                            status.launch_local_epoch)
                        .hex("launch_peer_epoch",
                            status.launch_peer_epoch)
                        .hex("launch_local_lifecycle_digest",
                            status.launch_local_lifecycle_digest)
                        .hex("launch_peer_lifecycle_digest",
                            status.launch_peer_lifecycle_digest)
                        .uinteger("stock_observer_last_presence",
                            status.stock_observer_last_presence)
                        .uinteger("stock_precontrol_battle_main_state",
                            status.stock_precontrol_battle_main_state)
                        .uinteger("stock_precontrol_battle_status",
                            status.stock_precontrol_battle_status)
                        .boolean("stock_precontrol_manager_ready",
                            status.stock_precontrol_manager_ready)
                        .boolean("stock_precontrol_stage_ready",
                            status.stock_precontrol_stage_ready)
                        .boolean("stock_precontrol_fighters_ready",
                            status.stock_precontrol_fighters_ready)
                        .boolean("stock_precontrol_auto_advance_clear",
                            status.stock_precontrol_auto_advance_clear)
                        .boolean("stock_precontrol_round_identity_ready",
                            status.stock_precontrol_round_identity_ready)
                        .boolean("stock_precontrol_stage_identity_ready",
                            status.stock_precontrol_stage_identity_ready)
                        .boolean("stock_precontrol_native_slot_ready",
                            status.stock_precontrol_native_slot_ready)
                        .string("stock_identity_capture_failure",
                            status.stock_identity_capture_failure)
                        .boolean("lobby_return_requested",
                            status.lobby_return_requested)
                        .boolean("lobby_return_dispatched",
                            status.lobby_return_dispatched)
                        .boolean("lobby_return_succeeded",
                            status.lobby_return_succeeded)
                        .uinteger("saves", status.saves)
                        .uinteger("loads", status.loads)
                        .uinteger("advances", status.advances)
                        .uinteger("rollback_advances",
                            status.rollback_advances)
                        .uinteger("gekko_correction_flush_calls",
                            status.gekko_correction_flush_calls)
                        .uinteger("pair_accepts", status.pair_accepts)
                        .boolean("summary_consensus_expected_valid",
                            status.summary_consensus_expected_valid)
                        .uinteger("summary_consensus_expected",
                            status.summary_consensus_expected)
                        .uinteger("summary_ack_windows_received",
                            status.summary_ack_windows_received)
                        .uinteger("summary_embedded_ack_windows_received",
                            status.summary_embedded_ack_windows_received)
                        .uinteger("summary_ack_window_last_next_unmatched",
                            status.summary_ack_window_last_next_unmatched)
                        .uinteger(
                            "summary_ack_window_cumulative_frames_observed",
                            status.summary_ack_window_cumulative_frames_observed)
                        .uinteger(
                            "summary_ack_window_selective_frames_observed",
                            status.summary_ack_window_selective_frames_observed)
                        .uinteger(
                            "summary_ack_window_frames_missing_locally",
                            status.summary_ack_window_frames_missing_locally)
                        .uinteger(
                            "summary_consensus_backpressure_ticks",
                            status.summary_consensus_backpressure_ticks)
                        .uinteger("terminal_frontier_recovery_ticks",
                            status.terminal_frontier_recovery_ticks)
                        .uinteger("terminal_frontier_recovery_retries",
                            status.terminal_frontier_recovery_retries)
                        .uinteger("terminal_frontier_recovery_deferrals",
                            status.terminal_frontier_recovery_deferrals)
                        .uinteger("terminal_quiesced_evidence_retries",
                            status.terminal_quiesced_evidence_retries)
                        .uinteger("terminal_pair_proof_accepts",
                            status.terminal_pair_proof_accepts)
                        .boolean("terminal_pair_proof_tail_finalized",
                            status.terminal_pair_proof_tail_finalized)
                        .uinteger("terminal_tail_evidence_sent",
                            status.terminal_tail_evidence_sent)
                        .uinteger("terminal_tail_evidence_received",
                            status.terminal_tail_evidence_received)
                        .uinteger("terminal_tail_evidence_matches",
                            status.terminal_tail_evidence_matches)
                        .uinteger("terminal_pair_proof_scan_start",
                            status.terminal_pair_proof_scan_start)
                        .uinteger("terminal_pair_proof_scan_terminal",
                            status.terminal_pair_proof_scan_terminal)
                        .uinteger("terminal_pair_proof_first_missing_frame",
                            status.terminal_pair_proof_first_missing_frame)
                        .hex("terminal_pair_proof_first_missing_mask",
                            status.terminal_pair_proof_first_missing_mask)
                        .uinteger("terminal_pair_proof_missing_local_frame",
                            status.terminal_pair_proof_missing_local_frame)
                        .uinteger("terminal_pair_proof_missing_remote_frame",
                            status.terminal_pair_proof_missing_remote_frame)
                        .uinteger("terminal_pair_proof_missing_pair_frame",
                            status.terminal_pair_proof_missing_pair_frame)
                        .hex("terminal_pair_proof_missing_remote_flags",
                            status.terminal_pair_proof_missing_remote_flags)
                        .uinteger("terminal_pair_proof_missing_local_result",
                            status.terminal_pair_proof_missing_local_result)
                        .uinteger("terminal_pair_proof_missing_remote_result",
                            status.terminal_pair_proof_missing_remote_result)
                        .uinteger("summary_collision_incoming_frame",
                            status.summary_collision_incoming_frame)
                        .uinteger("summary_collision_slot",
                            status.summary_collision_slot)
                        .boolean("summary_collision_local_valid",
                            status.summary_collision_local_valid)
                        .uinteger("summary_collision_local_frame",
                            status.summary_collision_local_frame)
                        .uinteger("summary_collision_local_flags",
                            status.summary_collision_local_flags)
                        .boolean("summary_collision_remote_valid",
                            status.summary_collision_remote_valid)
                        .uinteger("summary_collision_remote_frame",
                            status.summary_collision_remote_frame)
                        .uinteger("summary_collision_remote_flags",
                            status.summary_collision_remote_flags)
                        .uinteger("palette_variant_active_mask",
                            status.palette_variant_active_mask)
                        .hex("palette_variant_canonical_hash",
                            status.palette_variant_canonical_hash)
                        .hex("palette_variant_local_integrity_hash",
                            status.palette_variant_local_integrity_hash)
                        .uinteger("palette_variant_writer_observations",
                            status.palette_variant_writer_observations)
                        .uinteger("palette_variant_writer_failures",
                            status.palette_variant_writer_failures)
                        .uinteger("palette_variant_writer_last_serial",
                            status.palette_variant_writer_last_serial)
                        .uinteger(
                            "palette_variant_writer_last_observation",
                            status.palette_variant_writer_last_observation)
                        .integer("mismatch_frame",
                            status.mismatch_frame.valid
                                ? static_cast<int64_t>(
                                    status.mismatch_frame.value)
                                : -1)
                        .hex("mismatch_local_canonical_hash",
                            status.mismatch_local_canonical_hash)
                        .hex("mismatch_peer_canonical_hash",
                            status.mismatch_peer_canonical_hash)
                        .hex("mismatch_local_hgcpu_hash",
                            status.mismatch_local_component_hash[0])
                        .hex("mismatch_peer_hgcpu_hash",
                            status.mismatch_peer_component_hash[0])
                        .hex("mismatch_hgcpu_peer_mask",
                            status.mismatch_hgcpu_peer_mask)
                        .hex("mismatch_hgcpu_chara_chunk_mask",
                            status.mismatch_hgcpu_chara_chunk_mask)
                        .hex("mismatch_hgcpu_motion_contribution_mask",
                            status.mismatch_hgcpu_motion_contribution_mask)
                        .hex("mismatch_local_hgcpu_chara_0",
                            status.mismatch_local_hgcpu_peer
                                .chara_stream_hash[0])
                        .hex("mismatch_peer_hgcpu_chara_0",
                            status.mismatch_peer_hgcpu_peer
                                .chara_stream_hash[0])
                        .hex("mismatch_local_hgcpu_chara_1",
                            status.mismatch_local_hgcpu_peer
                                .chara_stream_hash[1])
                        .hex("mismatch_peer_hgcpu_chara_1",
                            status.mismatch_peer_hgcpu_peer
                                .chara_stream_hash[1])
                        .hex("mismatch_local_hgcpu_khit_0",
                            status.mismatch_local_hgcpu_peer.khit_hash[0])
                        .hex("mismatch_peer_hgcpu_khit_0",
                            status.mismatch_peer_hgcpu_peer.khit_hash[0])
                        .hex("mismatch_local_hgcpu_khit_1",
                            status.mismatch_local_hgcpu_peer.khit_hash[1])
                        .hex("mismatch_peer_hgcpu_khit_1",
                            status.mismatch_peer_hgcpu_peer.khit_hash[1])
                        .hex("mismatch_local_hgcpu_motion",
                            status.mismatch_local_hgcpu_peer.motion_slot_hash)
                        .hex("mismatch_peer_hgcpu_motion",
                            status.mismatch_peer_hgcpu_peer.motion_slot_hash)
                        .uinteger("mismatch_local_hgcpu_motion_p0_b0_age",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_age[0][0])
                        .uinteger("mismatch_peer_hgcpu_motion_p0_b0_age",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_age[0][0])
                        .hex("mismatch_local_hgcpu_motion_p0_b0_hash",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_hash[0][0])
                        .hex("mismatch_peer_hgcpu_motion_p0_b0_hash",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_hash[0][0])
                        .uinteger("mismatch_local_hgcpu_motion_p0_b1_age",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_age[0][1])
                        .uinteger("mismatch_peer_hgcpu_motion_p0_b1_age",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_age[0][1])
                        .hex("mismatch_local_hgcpu_motion_p0_b1_hash",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_hash[0][1])
                        .hex("mismatch_peer_hgcpu_motion_p0_b1_hash",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_hash[0][1])
                        .uinteger("mismatch_local_hgcpu_motion_p1_b0_age",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_age[1][0])
                        .uinteger("mismatch_peer_hgcpu_motion_p1_b0_age",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_age[1][0])
                        .hex("mismatch_local_hgcpu_motion_p1_b0_hash",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_hash[1][0])
                        .hex("mismatch_peer_hgcpu_motion_p1_b0_hash",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_hash[1][0])
                        .uinteger("mismatch_local_hgcpu_motion_p1_b1_age",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_age[1][1])
                        .uinteger("mismatch_peer_hgcpu_motion_p1_b1_age",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_age[1][1])
                        .hex("mismatch_local_hgcpu_motion_p1_b1_hash",
                            status.mismatch_local_hgcpu_peer
                                .motion_provider_hash[1][1])
                        .hex("mismatch_peer_hgcpu_motion_p1_b1_hash",
                            status.mismatch_peer_hgcpu_peer
                                .motion_provider_hash[1][1])
                        .hex("mismatch_local_hgcpu_secondary",
                            status.mismatch_local_hgcpu_peer
                                .secondary_event_hash)
                        .hex("mismatch_peer_hgcpu_secondary",
                            status.mismatch_peer_hgcpu_peer
                                .secondary_event_hash)
                        .hex("mismatch_local_hgcpu_timer_shape",
                            status.mismatch_local_hgcpu_peer.timer_shape_hash)
                        .hex("mismatch_peer_hgcpu_timer_shape",
                            status.mismatch_peer_hgcpu_peer.timer_shape_hash)
                        .hex("mismatch_local_hgcpu_skeleton_shape",
                            status.mismatch_local_hgcpu_peer
                                .skeleton_shape_hash)
                        .hex("mismatch_peer_hgcpu_skeleton_shape",
                            status.mismatch_peer_hgcpu_peer
                                .skeleton_shape_hash)
                        .hex("mismatch_local_motion_decode_scratch",
                            status.mismatch_local_hgcpu_peer
                                .motion_decode_scratch_hash)
                        .hex("mismatch_peer_motion_decode_scratch",
                            status.mismatch_peer_hgcpu_peer
                                .motion_decode_scratch_hash)
                        .hex("mismatch_local_motion_pose_residue",
                            status.mismatch_local_hgcpu_peer
                                .motion_pose_residue_hash)
                        .hex("mismatch_peer_motion_pose_residue",
                            status.mismatch_peer_hgcpu_peer
                                .motion_pose_residue_hash)
                        .uinteger("mismatch_local_hgcpu_effective_bytes",
                            status.mismatch_local_hgcpu_peer.effective_bytes)
                        .uinteger("mismatch_peer_hgcpu_effective_bytes",
                            status.mismatch_peer_hgcpu_peer.effective_bytes)
                        .uinteger("mismatch_local_hgcpu_khit_nodes_0",
                            status.mismatch_local_hgcpu_peer
                                .khit_node_count[0])
                        .uinteger("mismatch_peer_hgcpu_khit_nodes_0",
                            status.mismatch_peer_hgcpu_peer
                                .khit_node_count[0])
                        .uinteger("mismatch_local_hgcpu_khit_nodes_1",
                            status.mismatch_local_hgcpu_peer
                                .khit_node_count[1])
                        .uinteger("mismatch_peer_hgcpu_khit_nodes_1",
                            status.mismatch_peer_hgcpu_peer
                                .khit_node_count[1])
                        .hex("mismatch_local_explicit_hash",
                            status.mismatch_local_component_hash[1])
                        .hex("mismatch_peer_explicit_hash",
                            status.mismatch_peer_component_hash[1])
                        .hex("mismatch_local_stage_hash",
                            status.mismatch_local_component_hash[2])
                        .hex("mismatch_peer_stage_hash",
                            status.mismatch_peer_component_hash[2])
                        .hex("mismatch_local_wind_hash",
                            status.mismatch_local_component_hash[3])
                        .hex("mismatch_peer_wind_hash",
                            status.mismatch_peer_component_hash[3])
                        .hex("mismatch_explicit_range_mask",
                            status.mismatch_explicit_range_mask)
                        .hex("mismatch_local_ccpu_hash",
                            status.mismatch_local_ccpu_hash)
                        .hex("mismatch_peer_ccpu_hash",
                            status.mismatch_peer_ccpu_hash)
                        .hex("mismatch_local_native_round_state_hash",
                            status.mismatch_local_native_round_state_hash)
                        .hex("mismatch_peer_native_round_state_hash",
                            status.mismatch_peer_native_round_state_hash)
                        .hex("mismatch_local_native_simulation_state_hash",
                            status.mismatch_local_native_simulation_state_hash)
                        .hex("mismatch_peer_native_simulation_state_hash",
                            status.mismatch_peer_native_simulation_state_hash)
                        .uinteger(
                            "mismatch_local_palette_variant_active_mask",
                            status.mismatch_local_palette_variant_active_mask)
                        .uinteger(
                            "mismatch_peer_palette_variant_active_mask",
                            status.mismatch_peer_palette_variant_active_mask)
                        .hex(
                            "mismatch_local_palette_variant_canonical_hash",
                            status
                                .mismatch_local_palette_variant_canonical_hash)
                        .hex(
                            "mismatch_peer_palette_variant_canonical_hash",
                            status
                                .mismatch_peer_palette_variant_canonical_hash)
                        .hex("mismatch_local_input_0",
                            status.mismatch_local_input[0])
                        .hex("mismatch_local_input_1",
                            status.mismatch_local_input[1])
                        .hex("mismatch_peer_input_0",
                            status.mismatch_peer_input[0])
                        .hex("mismatch_peer_input_1",
                            status.mismatch_peer_input[1])
                        .hex("local_input_hash", status.local_input_hash)
                        .hex("remote_input_hash", status.remote_input_hash)
                        .uinteger("local_input_count",
                            status.local_input_count)
                        .uinteger("remote_input_count",
                            status.remote_input_count)
                        .uinteger("native_local_nonzero_inputs",
                            status.native_local_nonzero_inputs)
                        .uinteger("native_remote_nonzero_inputs",
                            status.native_remote_nonzero_inputs)
                        .uinteger("native_local_input_transitions",
                            status.native_local_input_transitions)
                        .uinteger("native_remote_input_transitions",
                            status.native_remote_input_transitions)
                        .hex("native_last_local_input",
                            status.native_last_local_input)
                        .hex("native_last_remote_input",
                            status.native_last_remote_input)
                        .uinteger("fixture_nonzero_inputs",
                            status.fixture_nonzero_inputs)
                        .uinteger("fixture_inputs_observed",
                            status.fixture_inputs_observed)
                        .uinteger("fixture_basic_actions_observed",
                            status.fixture_basic_actions_observed)
                        .uinteger("fixture_engine_inputs_observed",
                            status.fixture_engine_inputs_observed)
                        .uinteger("fixture_engine_input_mismatches",
                            status.fixture_engine_input_mismatches)
                        .hex("fixture_last_engine_input_0",
                            status.fixture_last_engine_input[0])
                        .hex("fixture_last_engine_input_1",
                            status.fixture_last_engine_input[1])
                        .hex("fixture_action_engine_input_0",
                            status.fixture_action_engine_input[0])
                        .hex("fixture_action_engine_input_1",
                            status.fixture_action_engine_input[1])
                        .uinteger("fixture_chara_inputs_observed",
                            status.fixture_chara_inputs_observed)
                        .uinteger("fixture_chara_input_mismatches",
                            status.fixture_chara_input_mismatches)
                        .hex("fixture_last_chara_input_0",
                            status.fixture_last_chara_input[0])
                        .hex("fixture_last_chara_input_1",
                            status.fixture_last_chara_input[1])
                        .hex("fixture_action_chara_input_0",
                            status.fixture_action_chara_input[0])
                        .hex("fixture_action_chara_input_1",
                            status.fixture_action_chara_input[1])
                        .uinteger("fixture_basic_action_edges_observed",
                            status.fixture_basic_action_edges_observed)
                        .uinteger(
                            "fixture_chara_secondary_input_mismatches",
                            status.fixture_chara_secondary_input_mismatches)
                        .hex("fixture_last_chara_secondary_input_0",
                            status.fixture_last_chara_secondary_input[0])
                        .hex("fixture_last_chara_secondary_input_1",
                            status.fixture_last_chara_secondary_input[1])
                        .hex("fixture_action_chara_secondary_input_0",
                            status.fixture_action_chara_secondary_input[0])
                        .hex("fixture_action_chara_secondary_input_1",
                            status.fixture_action_chara_secondary_input[1])
                        .boolean("fixture_distance_valid",
                            status.fixture_distance_valid)
                        .boolean("fixture_action_distance_valid",
                            status.fixture_action_distance_valid)
                        .real("fixture_initial_distance",
                            status.fixture_initial_distance)
                        .real("fixture_minimum_distance",
                            status.fixture_minimum_distance)
                        .uinteger("fixture_minimum_distance_frame",
                            status.fixture_minimum_distance_frame)
                        .real("fixture_action_distance",
                            status.fixture_action_distance)
                        .uinteger("fixture_move_samples",
                            status.fixture_move_samples)
                        .uinteger("fixture_pre_action_move_index_0",
                            status.fixture_pre_action_move_index[0])
                        .uinteger("fixture_pre_action_move_index_1",
                            status.fixture_pre_action_move_index[1])
                        .uinteger("fixture_action_move_index_0",
                            status.fixture_action_move_index[0])
                        .uinteger("fixture_action_move_index_1",
                            status.fixture_action_move_index[1])
                        .uinteger("fixture_action_move_changes_0",
                            status.fixture_action_move_changes[0])
                        .uinteger("fixture_action_move_changes_1",
                            status.fixture_action_move_changes[1])
                        .uinteger("fixture_active_attack_cell_observations_0",
                            status.fixture_active_attack_cell_observations[0])
                        .uinteger("fixture_active_attack_cell_observations_1",
                            status.fixture_active_attack_cell_observations[1])
                        .uinteger("fixture_first_active_attack_frame_0",
                            status.fixture_first_active_attack_frame[0])
                        .uinteger("fixture_first_active_attack_frame_1",
                            status.fixture_first_active_attack_frame[1])
                        .integer("fixture_last_packed_move_0",
                            status.fixture_last_packed_move[0])
                        .integer("fixture_last_packed_move_1",
                            status.fixture_last_packed_move[1])
                        .real("fixture_last_move_anim_frame_0",
                            status.fixture_last_move_anim_frame[0])
                        .real("fixture_last_move_anim_frame_1",
                            status.fixture_last_move_anim_frame[1])
                        .uinteger("fixture_packets_held",
                            status.fixture_packets_held)
                        .uinteger("fixture_packets_released",
                            status.fixture_packets_released)
                        .boolean("fixture_barrier_local_ready",
                            status.fixture_barrier_local_ready)
                        .boolean("fixture_barrier_peer_ready",
                            status.fixture_barrier_peer_ready)
                        .uinteger("fixture_barrier_wait_ticks",
                            status.fixture_barrier_wait_ticks)
                        .uinteger("fixture_barrier_owner_lead_ticks",
                            status.fixture_barrier_owner_lead_ticks)
                        .uinteger("fixture_scheduled_submission_frame",
                            status.fixture_scheduled_submission_frame)
                        .uinteger("fixture_first_held_submission_frame",
                            status.fixture_first_held_submission_frame)
                        .uinteger("fixture_last_held_submission_frame",
                            status.fixture_last_held_submission_frame)
                        .uinteger("fixture_first_delayed_input_frame",
                            status.fixture_first_delayed_input_frame)
                        .uinteger("fixture_last_delayed_input_frame",
                            status.fixture_last_delayed_input_frame)
                        .uinteger("fixture_correction_load_frame",
                            status.fixture_correction_load_frame)
                        .uinteger("fixture_rollback_replay_high_water",
                            status.fixture_rollback_replay_high_water)
                        .uinteger("fixture_load_candidates",
                            status.fixture_load_candidates)
                        .uinteger("fixture_load_promotions",
                            status.fixture_load_promotions)
                        .uinteger("fixture_load_rejections",
                            status.fixture_load_rejections)
                        .uinteger("fixture_discarded_audio_frame",
                            status.fixture_discarded_effect_frame[0])
                        .uinteger("fixture_discarded_vfx_frame",
                            status.fixture_discarded_effect_frame[1])
                        .uinteger("fixture_discarded_camera_frame",
                            status.fixture_discarded_effect_frame[2])
                        .uinteger("fixture_discarded_transition_frame",
                            status.fixture_discarded_effect_frame[3])
                        .uinteger("fixture_replayed_audio_frame",
                            status.fixture_replayed_effect_frame[0])
                        .uinteger("fixture_replayed_vfx_frame",
                            status.fixture_replayed_effect_frame[1])
                        .uinteger("fixture_replayed_camera_frame",
                            status.fixture_replayed_effect_frame[2])
                        .uinteger("fixture_replayed_transition_frame",
                            status.fixture_replayed_effect_frame[3])
                        .boolean("fixture_prediction_diverged",
                            status.fixture_prediction_diverged)
                        .boolean("fixture_converged",
                            status.fixture_converged)
                        .uinteger("fixture_converged_frame",
                            status.fixture_converged_frame)
                        .hex("fixture_converged_canonical_hash",
                            status.fixture_converged_canonical_hash)
                        .hex("fixture_converged_local_input_hash",
                            status.fixture_converged_local_input_hash)
                        .hex("fixture_converged_remote_input_hash",
                            status.fixture_converged_remote_input_hash)
                        .boolean("replay_input_enabled",
                            status.replay_input_enabled)
                        .boolean("replay_input_loaded",
                            status.replay_input_loaded)
                        .boolean("replay_match_base_seed_verified",
                            status.replay_match_base_seed_verified)
                        .hex("replay_match_base_seed_expected",
                            status.replay_match_base_seed_expected)
                        .hex("replay_match_base_seed_observed",
                            status.replay_match_base_seed_observed)
                        .uinteger("replay_rng_baseline_applies",
                            status.replay_rng_baseline_applies)
                        .boolean("replay_rng_baseline_verified",
                            status.replay_rng_baseline_verified)
                        .uinteger("replay_rng_baseline_round",
                            status.replay_rng_baseline_round)
                        .uinteger("replay_rng_baseline_generation",
                            status.replay_rng_baseline_generation)
                        .hex("replay_rng_lcg_expected",
                            status.replay_rng_lcg_expected)
                        .hex("replay_rng_lcg_observed",
                            status.replay_rng_lcg_observed)
                        .hex("replay_rng_lfsr_hash_expected",
                            status.replay_rng_lfsr_hash_expected)
                        .hex("replay_rng_lfsr_hash_observed",
                            status.replay_rng_lfsr_hash_observed)
                        .uinteger("replay_rng_lfsr_index_expected",
                            status.replay_rng_lfsr_index_expected)
                        .uinteger("replay_rng_lfsr_index_observed",
                            status.replay_rng_lfsr_index_observed)
                        .uinteger("replay_motion_pose_baseline_applies",
                            status.replay_motion_pose_baseline_applies)
                        .boolean("replay_motion_pose_baseline_verified",
                            status.replay_motion_pose_baseline_verified)
                        .uinteger("replay_motion_pose_baseline_round",
                            status.replay_motion_pose_baseline_round)
                        .uinteger("replay_motion_pose_baseline_generation",
                            status.replay_motion_pose_baseline_generation)
                        .hex("replay_motion_pose_hash_expected",
                            status.replay_motion_pose_hash_expected)
                        .hex("replay_motion_pose_hash_observed",
                            status.replay_motion_pose_hash_observed)
                        .integer("replay_motion_pose_last_player_expected",
                            status.replay_motion_pose_last_player_expected)
                        .integer("replay_motion_pose_last_player_observed",
                            status.replay_motion_pose_last_player_observed)
                        .hex("rng_crt_first_mismatch_rva",
                            status.rng_crt_first_mismatch_rva)
                        .uinteger("frame_zero_baseline_phase",
                            status.frame_zero_baseline_phase)
                        .boolean("frame_zero_launch_rng_verified",
                            status.frame_zero_launch_rng_verified)
                        .boolean("frame_zero_baseline_save_verified",
                            status.frame_zero_baseline_save_verified)
                        .boolean("frame_zero_pre_advance_verified",
                            status.frame_zero_pre_advance_verified)
                        .uinteger("frame_zero_restore_attempts",
                            status.frame_zero_restore_attempts)
                        .uinteger("frame_zero_drift_repairs",
                            status.frame_zero_drift_repairs)
                        .uinteger("frame_zero_restore_verifications",
                            status.frame_zero_restore_verifications)
                        .uinteger("frame_zero_checkpoint_failures",
                            status.frame_zero_checkpoint_failures)
                        .hex("frame_zero_launch_lcg",
                            status.frame_zero_launch_lcg)
                        .hex("frame_zero_launch_lfsr_hash",
                            status.frame_zero_launch_lfsr_hash)
                        .uinteger("frame_zero_launch_lfsr_index",
                            status.frame_zero_launch_lfsr_index)
                        .hex("frame_zero_launch_gameplay_crt_state",
                            status.frame_zero_launch_gameplay_crt_state)
                        .hex("frame_zero_launch_gameplay_crt_seed",
                            status.frame_zero_launch_gameplay_crt_seed)
                        .uinteger(
                            "frame_zero_launch_gameplay_crt_draw_ordinal",
                            status.frame_zero_launch_gameplay_crt_draw_ordinal)
                        .hex("frame_zero_save_lcg",
                            status.frame_zero_save_lcg)
                        .hex("frame_zero_save_lfsr_hash",
                            status.frame_zero_save_lfsr_hash)
                        .uinteger("frame_zero_save_lfsr_index",
                            status.frame_zero_save_lfsr_index)
                        .hex("frame_zero_pre_advance_lcg",
                            status.frame_zero_pre_advance_lcg)
                        .hex("frame_zero_pre_advance_lfsr_hash",
                            status.frame_zero_pre_advance_lfsr_hash)
                        .uinteger("frame_zero_pre_advance_lfsr_index",
                            status.frame_zero_pre_advance_lfsr_index)
                        .boolean("replay_input_exhausted",
                            status.replay_input_exhausted)
                        .uinteger("replay_input_player",
                            status.replay_input_player)
                        .uinteger("replay_input_round",
                            status.replay_input_round)
                        .uinteger("replay_input_active_round",
                            status.replay_input_active_round)
                        .uinteger("replay_input_round_advances",
                            status.replay_input_round_advances)
                        .uinteger("replay_input_start_frame",
                            status.replay_input_start_frame)
                        .uinteger("replay_input_round_frames",
                            status.replay_input_round_frames)
                        .uinteger("replay_input_last_index",
                            status.replay_input_last_index)
                        .boolean("replay_input_first_observed",
                            status.replay_input_first_observed)
                        .uinteger("replay_input_first_submission_frame",
                            status.replay_input_first_submission_frame)
                        .uinteger("replay_input_first_index",
                            status.replay_input_first_index)
                        .uinteger("replay_input_last_submission_frame",
                            status.replay_input_last_submission_frame)
                        .uinteger("replay_input_target_rollback_depth",
                            status.replay_input_target_rollback_depth)
                        .uinteger("replay_input_submissions",
                            status.replay_input_submissions)
                        .uinteger("replay_input_nonzero_submissions",
                            status.replay_input_nonzero_submissions)
                        .hex("replay_input_submission_hash",
                            status.replay_input_submission_hash)
                        .uinteger("replay_consumed_checks",
                            status.replay_consumed_checks)
                        .uinteger("replay_consumed_mismatches",
                            status.replay_consumed_mismatches)
                        .uinteger("replay_consumed_neutral_tail_checks",
                            status.replay_consumed_neutral_tail_checks)
                        .uinteger("replay_confirmed_neutral_tail_checks",
                            status.replay_confirmed_neutral_tail_checks)
                        .uinteger("replay_prediction_checks",
                            status.replay_prediction_checks)
                        .uinteger("replay_prediction_mismatches",
                            status.replay_prediction_mismatches)
                        .boolean("replay_first_consumed_observed",
                            status.replay_first_consumed_observed)
                        .uinteger("replay_first_consumed_logical_frame",
                            status.replay_first_consumed_logical_frame)
                        .uinteger("replay_first_consumed_index",
                            status.replay_first_consumed_index)
                        .hex("replay_first_consumed_expected_p0",
                            status.replay_first_consumed_expected[0])
                        .hex("replay_first_consumed_expected_p1",
                            status.replay_first_consumed_expected[1])
                        .hex("replay_first_consumed_actual_p0",
                            status.replay_first_consumed_actual[0])
                        .hex("replay_first_consumed_actual_p1",
                            status.replay_first_consumed_actual[1])
                        .boolean("replay_round_frame_zero_observed",
                            status.replay_round_frame_zero_observed)
                        .uinteger("replay_round_frame_zero_logical_frame",
                            status.replay_round_frame_zero_logical_frame)
                        .uinteger("replay_round_frame_zero_source_index",
                            status.replay_round_frame_zero_source_index)
                        .uinteger("replay_round_frame_zero_replay_round",
                            status.replay_round_frame_zero_replay_round)
                        .uinteger("replay_round_frame_zero_generation",
                            status.replay_round_frame_zero_generation)
                        .hex("replay_round_frame_zero_expected_p0",
                            status.replay_round_frame_zero_expected[0])
                        .hex("replay_round_frame_zero_expected_p1",
                            status.replay_round_frame_zero_expected[1])
                        .hex("replay_round_frame_zero_actual_p0",
                            status.replay_round_frame_zero_actual[0])
                        .hex("replay_round_frame_zero_actual_p1",
                            status.replay_round_frame_zero_actual[1])
                        .hex("replay_input_round_hash_p0",
                            status.replay_input_round_hash[0])
                        .hex("replay_input_round_hash_p1",
                            status.replay_input_round_hash[1])
                        .uinteger("lifecycle_token_nanoseconds",
                            status.lifecycle_token_nanoseconds)
                        .uinteger("lifecycle_token_calls",
                            status.lifecycle_token_calls)
                        .uinteger("lifecycle_mismatch_mask",
                            status.lifecycle_mismatch_mask)
                        .uinteger("lifecycle_expected_round_ordinal",
                            status.lifecycle_expected_round_ordinal)
                        .uinteger("lifecycle_live_round_ordinal",
                            status.lifecycle_live_round_ordinal)
                        .uinteger("lifecycle_expected_battle_main_state",
                            status.lifecycle_expected_battle_main_state)
                        .uinteger("lifecycle_live_battle_main_state",
                            status.lifecycle_live_battle_main_state)
                        .uinteger("lifecycle_expected_battle_status",
                            status.lifecycle_expected_battle_status)
                        .uinteger("lifecycle_live_battle_status",
                            status.lifecycle_live_battle_status)
                        .boolean("lifecycle_expected_auto_advance",
                            status.lifecycle_expected_auto_advance)
                        .boolean("lifecycle_live_auto_advance",
                            status.lifecycle_live_auto_advance)
                        .hex("lifecycle_expected_round_start_digest",
                            status.lifecycle_expected_round_start_digest)
                        .hex("lifecycle_live_round_start_digest",
                            status.lifecycle_live_round_start_digest)
                        .uinteger("stock_round_transition_handoffs",
                            status.stock_round_transition_handoffs)
                        .uinteger("stock_round_transition_rearms",
                            status.stock_round_transition_rearms)
                        .string("round_phase_name",
                            RollbackRoundPhaseName(status.round_phase))
                        .uinteger("round_generation",
                            status.round_generation)
                        .uinteger("round_ordinal",
                            status.launch_round_ordinal)
                        .hex("round_epoch", status.round_epoch)
                        .hex("local_match_identity_digest",
                            status.local_match_identity_digest)
                        .hex("match_identity_digest",
                            status.match_identity_digest)
                        .uinteger("accepted_udp_generation",
                            status.accepted_udp_generation)
                        .uinteger("round_candidates_frozen",
                            status.round_candidates_frozen)
                        .uinteger("round_identities_captured",
                            status.round_identities_captured)
                        .uinteger("round_baselines_published",
                            status.round_baselines_published)
                        .uinteger("round_baselines_accepted",
                            status.round_baselines_accepted)
                        .uinteger("round_gekko_restarts",
                            status.round_gekko_restarts)
                        .uinteger("round_stock_pass_through_calls",
                            status.round_stock_pass_through_calls)
                        .uinteger("round_stale_packets_discarded",
                            status.round_stale_packets_discarded)
                        .boolean("native_terminal_hooks_installed",
                            status.native_terminal_hooks_installed)
                        .uinteger("native_simulation_calls_allowed",
                            status.native_simulation_calls_allowed)
                        .uinteger("native_simulation_calls_suppressed",
                            status.native_simulation_calls_suppressed)
                        .uinteger("native_owned_simulation_iterations",
                            status.native_owned_simulation_iterations)
                        .uinteger("native_owned_per_frame_calls",
                            status.native_owned_per_frame_calls)
                        .uinteger("native_owned_chara_per_tick_calls",
                            status.native_owned_chara_per_tick_calls)
                        .uinteger("native_stock_chara_per_tick_calls",
                            status.native_stock_chara_per_tick_calls)
                        .uinteger(
                            "native_unowned_chara_per_tick_calls",
                            status.native_unowned_chara_per_tick_calls)
                        .uinteger(
                            "native_unowned_chara_per_tick_suppressions",
                            status.native_unowned_chara_per_tick_suppressions)
                        .uinteger(
                            "native_unowned_chara_per_tick_mutations",
                            status.native_unowned_chara_per_tick_mutations)
                        .uinteger(
                            "native_animation_dispatch_rejections",
                            status.native_animation_dispatch_rejections)
                        .uinteger(
                            "native_clip_owner_frame_mismatches",
                            status.native_clip_owner_frame_mismatches)
                        .boolean(
                            "camera_timer_action_manager_alias_verified",
                            status
                                .camera_timer_action_manager_alias_verified)
                        .uinteger("native_owned_input_pair_injections",
                            status.native_owned_input_pair_injections)
                        .uinteger("native_input_source_overrides",
                            status.native_input_source_overrides)
                        .uinteger("native_input_source_restores",
                            status.native_input_source_restores)
                        .uinteger("native_simulation_cursor_restores",
                            status.native_simulation_cursor_restores)
                        .uinteger("native_simulation_callback_guard_checks",
                            status.native_simulation_callback_guard_checks)
                        .boolean("native_input_pair_failure_evidence_valid",
                            status.native_input_pair_failure_evidence_valid)
                        .boolean("native_input_pair_failure_boundary_matches",
                            status.native_input_pair_failure_boundary_matches)
                        .hex("native_input_pair_failure_collection",
                            status.native_input_pair_failure_collection)
                        .hex("native_input_pair_failure_expected_collection",
                            status.native_input_pair_failure_expected_collection)
                        .hex("native_input_pair_failure_header",
                            status.native_input_pair_failure_header)
                        .hex("native_input_pair_failure_expected_header",
                            status.native_input_pair_failure_expected_header)
                        .uinteger("native_input_pair_failure_successful_injections",
                            status.native_input_pair_failure_successful_injections)
                        .integer("native_input_pair_failure_before_input_last_frame",
                            status.native_input_pair_failure_before_input_last_frame)
                        .uinteger("native_input_pair_failure_before_input_master",
                            status.native_input_pair_failure_before_input_master)
                        .integer("native_input_pair_failure_armed_battle_last_frame",
                            status.native_input_pair_failure_armed_battle_last_frame)
                        .uinteger("native_input_pair_failure_armed_battle_last_applied",
                            status.native_input_pair_failure_armed_battle_last_applied)
                        .boolean("native_input_pair_failure_current_input_last_frame_read",
                            status.native_input_pair_failure_current_input_last_frame_read)
                        .boolean("native_input_pair_failure_current_input_master_read",
                            status.native_input_pair_failure_current_input_master_read)
                        .boolean("native_input_pair_failure_current_battle_last_frame_read",
                            status.native_input_pair_failure_current_battle_last_frame_read)
                        .boolean("native_input_pair_failure_current_battle_last_applied_read",
                            status.native_input_pair_failure_current_battle_last_applied_read)
                        .integer("native_input_pair_failure_current_input_last_frame",
                            status.native_input_pair_failure_current_input_last_frame)
                        .uinteger("native_input_pair_failure_current_input_master",
                            status.native_input_pair_failure_current_input_master)
                        .integer("native_input_pair_failure_current_battle_last_frame",
                            status.native_input_pair_failure_current_battle_last_frame)
                        .uinteger("native_input_pair_failure_current_battle_last_applied",
                            status.native_input_pair_failure_current_battle_last_applied)
                        .uinteger("native_callback_coverage_checks",
                            status.native_callback_coverage_checks)
                        .uinteger("native_callback_derived_repairs",
                            status.native_callback_derived_repairs)
                        .uinteger("native_input_callback_snapshot_captures",
                            status.native_input_callback_snapshot_captures)
                        .uinteger("native_input_callback_snapshot_restores",
                            status.native_input_callback_snapshot_restores)
                        .uinteger("native_input_callback_action_mode",
                            status.native_input_callback_action_mode)
                        .boolean("native_callback_coverage_armed",
                            status.native_callback_coverage_armed)
                        .boolean("native_callback_coverage_failure_valid",
                            status.native_callback_coverage_failure.valid)
                        .uinteger("native_callback_coverage_failure_mask",
                            status.native_callback_coverage_failure.mismatch_mask)
                        .integer("native_callback_failure_input_count",
                            status.native_callback_coverage_failure.token
                                .input_callbacks.count)
                        .hex("native_callback_failure_input_entry_digest",
                            status.native_callback_coverage_failure.token
                                .input_callbacks.entry_digest)
                        .hex("native_callback_failure_input_target_digest",
                            status.native_callback_coverage_failure.token
                                .input_callbacks.target_digest)
                        .integer("native_callback_failure_simulation_count",
                            status.native_callback_coverage_failure.token
                                .simulation_callbacks.count)
                        .hex("native_callback_failure_simulation_entry_digest",
                            status.native_callback_coverage_failure.token
                                .simulation_callbacks.entry_digest)
                        .hex("native_callback_failure_simulation_target_digest",
                            status.native_callback_coverage_failure.token
                                .simulation_callbacks.target_digest)
                        .uinteger("native_callback_failure_loop_again",
                            status.native_callback_coverage_failure.token.loop_again)
                        .uinteger("native_callback_failure_pending_dispatch",
                            status.native_callback_coverage_failure.token
                                .pending_dispatch)
                        .integer("native_callback_failure_unpause_grace_period",
                            status.native_callback_coverage_failure.token
                                .unpause_grace_period)
                        .hex("native_callback_failure_input_slot_table",
                            status.native_callback_coverage_failure.input_state
                                .slot_table)
                        .integer("native_callback_failure_input_table_index",
                            status.native_callback_coverage_failure.input_state
                                .table_index)
                        .integer("native_callback_failure_input_slot_index",
                            status.native_callback_coverage_failure.input_state
                                .slot_index)
                        .uinteger("native_callback_failure_input_action_mode",
                            status.native_callback_coverage_failure.input_state
                                .action_mode)
                        .hex("native_callback_failure_input_state_digest",
                            status.native_callback_coverage_failure.input_state
                                .digest)
                        .boolean("native_owned_iteration_failure_valid",
                            status.native_owned_iteration_failure.valid)
                        .uinteger("native_owned_iteration_failure_kind",
                            static_cast<uint8_t>(
                                status.native_owned_iteration_failure.kind))
                        .uinteger("native_owned_iteration_failure_frame",
                            status.native_owned_iteration_failure.logical_frame)
                        .boolean("native_owned_iteration_failure_rolling_back",
                            status.native_owned_iteration_failure.rolling_back)
                        .uinteger("native_owned_iteration_failure_raw_mask",
                            status.native_owned_iteration_failure
                                .raw_coverage_mismatch)
                        .uinteger("native_owned_iteration_failure_effective_mask",
                            status.native_owned_iteration_failure
                                .effective_coverage_mismatch)
                        .integer("native_owned_iteration_failure_grace_before",
                            status.native_owned_iteration_failure.before
                                .unpause_grace_period)
                        .integer("native_owned_iteration_failure_grace_after",
                            status.native_owned_iteration_failure.after
                                .unpause_grace_period)
                        .uinteger("native_owned_iteration_failure_loop_before",
                            status.native_owned_iteration_failure.before
                                .loop_again)
                        .uinteger("native_owned_iteration_failure_loop_after",
                            status.native_owned_iteration_failure.after
                                .loop_again)
                        .uinteger("native_owned_iteration_failure_dispatch_before",
                            status.native_owned_iteration_failure.before
                                .pending_dispatch)
                        .uinteger("native_owned_iteration_failure_dispatch_after",
                            status.native_owned_iteration_failure.after
                                .pending_dispatch)
                        .boolean("native_owned_iteration_failure_terminal_pending_before",
                            status.native_owned_iteration_failure
                                .terminal_pending_before)
                        .boolean("native_owned_iteration_failure_terminal_pending_after",
                            status.native_owned_iteration_failure
                                .terminal_pending_after)
                        .boolean("native_owned_iteration_failure_producer_before_valid",
                            status.native_owned_iteration_failure
                                .producer_frame_before_valid)
                        .uinteger("native_owned_iteration_failure_producer_before",
                            status.native_owned_iteration_failure
                                .producer_frame_before)
                        .boolean("native_owned_iteration_failure_producer_after_valid",
                            status.native_owned_iteration_failure
                                .producer_frame_after_valid)
                        .uinteger("native_owned_iteration_failure_producer_after",
                            status.native_owned_iteration_failure
                                .producer_frame_after)
                        .uinteger("native_owned_iteration_failure_suppressions_before",
                            status.native_owned_iteration_failure
                                .notification_suppressions_before)
                        .uinteger("native_owned_iteration_failure_suppressions_after",
                            status.native_owned_iteration_failure
                                .notification_suppressions_after)
                        .boolean("native_owned_iteration_failure_completion_valid",
                            status.native_owned_iteration_failure
                                .completion_valid)
                        .uinteger("native_tick_hook_entries",
                            status.native_hook_entries[0])
                        .uinteger("native_tick_hook_pass_through",
                            status.native_hook_pass_through[0])
                        .uinteger("native_tick_hook_owned",
                            status.native_hook_owned[0])
                        .uinteger("native_input_pair_hook_entries",
                            status.native_hook_entries[1])
                        .uinteger("native_input_pair_hook_pass_through",
                            status.native_hook_pass_through[1])
                        .uinteger("native_input_pair_hook_owned",
                            status.native_hook_owned[1])
                        .uinteger("native_simulation_hook_entries",
                            status.native_hook_entries[2])
                        .uinteger("native_simulation_hook_pass_through",
                            status.native_hook_pass_through[2])
                        .uinteger("native_simulation_hook_owned",
                            status.native_hook_owned[2])
                        .uinteger("native_round_over_hook_entries",
                            status.native_hook_entries[3])
                        .uinteger("native_round_over_hook_pass_through",
                            status.native_hook_pass_through[3])
                        .uinteger("native_round_over_hook_owned",
                            status.native_hook_owned[3])
                        .uinteger("native_terminal_notify_hook_entries",
                            status.native_hook_entries[4])
                        .uinteger(
                            "native_terminal_notify_hook_pass_through",
                            status.native_hook_pass_through[4])
                        .uinteger("native_terminal_notify_hook_owned",
                            status.native_hook_owned[4])
                        .uinteger("native_new_round_finalize_hook_entries",
                            status.native_hook_entries[5])
                        .uinteger(
                            "native_new_round_finalize_hook_pass_through",
                            status.native_hook_pass_through[5])
                        .uinteger("native_new_round_finalize_hook_owned",
                            status.native_hook_owned[5])
                        .uinteger("native_pre_new_round_hook_entries",
                            status.native_hook_entries[6])
                        .uinteger(
                            "native_pre_new_round_hook_pass_through",
                            status.native_hook_pass_through[6])
                        .uinteger("native_pre_new_round_hook_owned",
                            status.native_hook_owned[6])
                        .uinteger("native_palette_writer_hook_entries",
                            status.native_hook_entries[7])
                        .uinteger(
                            "native_palette_writer_hook_pass_through",
                            status.native_hook_pass_through[7])
                        .uinteger("native_palette_writer_hook_owned",
                            status.native_hook_owned[7])
                        .uinteger("native_hook_callbacks_inflight",
                            status.native_hook_callbacks_inflight)
                        .uinteger("native_terminal_handoffs_released",
                            status.native_terminal_handoffs_released)
                        .uinteger("native_terminal_transitions_observed",
                            status.native_terminal_transitions_observed)
                        .uinteger(
                            "native_round_over_true_results_suppressed",
                            status.native_round_over_true_results_suppressed)
                        .uinteger(
                            "native_round_over_predicate_releases",
                            status.native_round_over_predicate_releases)
                        .uinteger(
                            "native_terminal_notifications_suppressed",
                            status.native_terminal_notifications_suppressed)
                        .uinteger(
                            "native_terminal_notifications_released",
                            status.native_terminal_notifications_released)
                        .uinteger(
                            "native_terminal_handoffs_without_notification",
                            status.native_terminal_handoffs_without_notification)
                        .uinteger(
                            "native_terminal_clock_alignments",
                            status.native_terminal_clock_alignments)
                        .uinteger(
                            "native_terminal_backlog_frames_discarded",
                            status.native_terminal_backlog_frames_discarded)
                        .uinteger(
                            "native_terminal_last_backlog_frames_discarded",
                            status.native_terminal_last_backlog_frames_discarded)
                        .integer(
                            "native_terminal_input_last_frame",
                            status.native_terminal_input_last_frame)
                        .uinteger(
                            "native_terminal_input_master_clock",
                            status.native_terminal_input_master_clock)
                        .integer(
                            "native_terminal_battle_last_frame_before",
                            status.native_terminal_battle_last_frame_before)
                        .uinteger(
                            "native_terminal_battle_last_applied_before",
                            status.native_terminal_battle_last_applied_before)
                        .uinteger(
                            "native_terminal_pending_delta_before",
                            status.native_terminal_pending_delta_before)
                        .uinteger(
                            "native_terminal_pending_delta_armed",
                            status.native_terminal_pending_delta_armed)
                        .uinteger(
                            "native_terminal_handoff_native_calls",
                            status.native_terminal_handoff_native_calls)
                        .uinteger(
                            "native_terminal_handoff_per_frame_calls",
                            status.native_terminal_handoff_per_frame_calls)
                        .uinteger(
                            "native_terminal_pending_delta_after",
                            status.native_terminal_pending_delta_after)
                        .boolean(
                            "native_terminal_handoff_delta_verified",
                            status.native_terminal_handoff_delta_verified)
                        .uinteger(
                            "native_terminal_handoff_delta_verifications",
                            status.native_terminal_handoff_delta_verifications)
                        .uinteger(
                            "native_inter_round_control_ticks_armed",
                            status.native_inter_round_control_ticks_armed)
                        .uinteger(
                            "native_inter_round_control_ticks_completed",
                            status.native_inter_round_control_ticks_completed)
                        .uinteger(
                            "native_inter_round_control_per_frame_calls",
                            status.native_inter_round_control_per_frame_calls)
                        .uinteger(
                            "native_inter_round_native_pass_through_calls",
                            status.native_inter_round_native_pass_through_calls)
                        .uinteger(
                            "native_inter_round_native_pass_through_per_frame_calls",
                            status.native_inter_round_native_pass_through_per_frame_calls)
                        .uinteger(
                            "native_inter_round_pass_through_backlog_frames_discarded",
                            status.native_inter_round_pass_through_backlog_frames_discarded)
                        .uinteger(
                            "native_inter_round_control_backlog_frames_discarded",
                            status.native_inter_round_control_backlog_frames_discarded)
                        .uinteger(
                            "native_inter_round_control_refusals",
                            status.native_inter_round_control_refusals)
                        .uinteger(
                            "native_inter_round_clock_waits",
                            status.native_inter_round_clock_waits)
                        .uinteger(
                            "native_inter_round_zero_delta_epoch_syncs",
                            status.native_inter_round_zero_delta_epoch_syncs)
                        .uinteger(
                            "native_inter_round_zero_delta_epoch_sync_generation",
                            status
                                .native_inter_round_zero_delta_epoch_sync_generation)
                        .uinteger(
                            "native_inter_round_current_active_status1_passes",
                            status
                                .native_inter_round_current_active_status1_passes)
                        .uinteger(
                            "native_inter_round_current_active_status3_passes",
                            status
                                .native_inter_round_current_active_status3_passes)
                        .uinteger(
                            "native_inter_round_new_round_armed_passes",
                            status.native_inter_round_new_round_armed_passes)
                        .uinteger(
                            "native_inter_round_new_round_due_finalize_passes",
                            status
                                .native_inter_round_new_round_due_finalize_passes)
                        .uinteger(
                            "native_initial_new_round_finalize_passes",
                            status.native_initial_new_round_finalize_passes)
                        .boolean(
                            "native_initial_new_round_transition_verified",
                            status.native_initial_new_round_transition_verified)
                        .boolean(
                            "native_initial_new_round_baseline_verified",
                            status.native_initial_new_round_baseline_verified)
                        .uinteger(
                            "secondary_event_authority_messages_sent",
                            status.secondary_event_authority_messages_sent)
                        .uinteger(
                            "secondary_event_authority_messages_received",
                            status.secondary_event_authority_messages_received)
                        .uinteger(
                            "secondary_event_authority_applies",
                            status.secondary_event_authority_applies)
                        .uinteger(
                            "secondary_event_authority_verifications",
                            status.secondary_event_authority_verifications)
                        .uinteger(
                            "secondary_event_authority_recovery_attempts",
                            status.secondary_event_authority_recovery_attempts)
                        .uinteger(
                            "secondary_event_authority_recovery_successes",
                            status.secondary_event_authority_recovery_successes)
                        .uinteger(
                            "secondary_event_authority_recovery_failures",
                            status.secondary_event_authority_recovery_failures)
                        .uinteger("motion_bank_authority_messages_sent",
                            status.motion_bank_authority_messages_sent)
                        .uinteger("motion_bank_authority_messages_received",
                            status.motion_bank_authority_messages_received)
                        .uinteger("motion_bank_authority_applies",
                            status.motion_bank_authority_applies)
                        .uinteger("motion_bank_authority_verifications",
                            status.motion_bank_authority_verifications)
                        .uinteger("stage_wind_authority_messages_sent",
                            status.stage_wind_authority_messages_sent)
                        .uinteger("stage_wind_authority_messages_received",
                            status.stage_wind_authority_messages_received)
                        .uinteger("stage_wind_authority_retransmissions",
                            status.stage_wind_authority_retransmissions)
                        .uinteger("stage_wind_authority_applies",
                            status.stage_wind_authority_applies)
                        .uinteger("stage_wind_authority_verifications",
                            status.stage_wind_authority_verifications)
                        .uinteger("stage_wind_authority_recovery_attempts",
                            status.stage_wind_authority_recovery_attempts)
                        .uinteger("stage_wind_authority_recovery_successes",
                            status.stage_wind_authority_recovery_successes)
                        .uinteger("stage_wind_authority_recovery_failures",
                            status.stage_wind_authority_recovery_failures)
                        .hex("stage_wind_authority_capture_id",
                            status.stage_wind_authority_capture_id)
                        .hex("stage_wind_authority_image_hash",
                            status.stage_wind_authority_image_hash)
                        .uinteger("stage_wind_authority_output_active",
                            status.stage_wind_authority_output_active)
                        .uinteger(
                            "stage_wind_authority_strict_mismatch_mask",
                            status.stage_wind_authority_strict_mismatch_mask)
                        .uinteger(
                            "stage_wind_authority_header_mismatch_mask",
                            status.stage_wind_authority_header_mismatch_mask)
                        .uinteger("stage_wind_authority_owner_node_count",
                            status.stage_wind_authority_owner_node_count)
                        .uinteger("stage_wind_authority_local_node_count",
                            status.stage_wind_authority_local_node_count)
                        .uinteger("stage_wind_authority_owner_emitter_count",
                            status.stage_wind_authority_owner_emitter_count)
                        .uinteger("stage_wind_authority_local_emitter_count",
                            status.stage_wind_authority_local_emitter_count)
                        .uinteger("stage_wind_authority_topology_rebuilds",
                            status.stage_wind_authority_topology_rebuilds)
                        .uinteger(
                            "stage_wind_authority_preflight_failure_mask",
                            status.stage_wind_authority_preflight_failure_mask)
                        .string("carried_state_authority_component_failure",
                            status.carried_state_authority_component_failure)
                        .string("stage_wind_authority_failure",
                            status.stage_wind_authority_failure)
                        .boolean(
                            "stage_wind_authority_local_send_complete",
                            status.stage_wind_authority_local_send_complete)
                        .boolean("stage_wind_authority_current_verified",
                            status.stage_wind_authority_current_verified)
                        .uinteger("native_pre_new_round_hook_arrivals",
                            status.native_pre_new_round_hook_arrivals)
                        .uinteger("native_pre_new_round_holds",
                            status.native_pre_new_round_holds)
                        .uinteger("native_pre_new_round_releases",
                            status.native_pre_new_round_releases)
                        .uinteger("native_pre_new_round_original_calls",
                            status.native_pre_new_round_original_calls)
                        .uinteger("native_pre_new_round_local_stage",
                            status.native_pre_new_round_local_stage)
                        .uinteger("native_pre_new_round_peer_stage",
                            status.native_pre_new_round_peer_stage)
                        .hex("native_pre_new_round_entry_digest",
                            status.native_pre_new_round_entry_digest)
                        .uinteger("native_pre_new_round_target_generation",
                            status.native_pre_new_round_target_generation)
                        .uinteger("native_pre_new_round_target_ordinal",
                            status.native_pre_new_round_target_ordinal)
                        .hex("native_pre_new_round_completed_pair_epoch",
                            status.native_pre_new_round_completed_pair_epoch)
                        .uinteger(
                            "native_pre_new_round_completed_epoch_clears",
                            status.native_pre_new_round_completed_epoch_clears)
                        .uinteger("native_pre_new_round_first_entry_serial",
                            status.native_pre_new_round_first_entry_serial)
                        .uinteger("native_pre_new_round_first_hold_serial",
                            status.native_pre_new_round_first_hold_serial)
                        .uinteger("native_pre_new_round_release_serial",
                            status.native_pre_new_round_release_serial)
                        .boolean(
                            "native_pre_new_round_post_release_verified",
                            status.native_pre_new_round_post_release_verified)
                        .uinteger(
                            "native_pre_new_round_reentry_validation_mask",
                            status.native_pre_new_round_reentry_validation_mask)
                        .uinteger(
                            "native_pre_new_round_reentry_control_mask",
                            status.native_pre_new_round_reentry_control_mask)
                        .uinteger(
                            "native_pre_new_round_shutdown_refusals",
                            status.native_pre_new_round_shutdown_refusals)
                        .uinteger(
                            "native_new_round_transition_deferrals",
                            status.native_new_round_transition_deferrals)
                        .uinteger(
                            "native_new_round_transition_releases",
                            status.native_new_round_transition_releases)
                        .uinteger(
                            "native_new_round_transition_reentries",
                            status.native_new_round_transition_reentries)
                        .uinteger(
                            "native_new_round_transition_shutdown_refusals",
                            status.native_new_round_transition_shutdown_refusals)
                        .uinteger(
                            "native_new_round_transition_frame",
                            status.native_new_round_transition_frame)
                        .uinteger(
                            "native_new_round_transition_timer",
                            status.native_new_round_transition_timer)
                        .uinteger(
                            "native_new_round_transition_release_serial",
                            status.native_new_round_transition_release_serial)
                        .uinteger(
                            "native_new_round_baseline_save_serial",
                            status.native_new_round_baseline_save_serial)
                        .uinteger(
                            "native_new_round_transition_release_generation",
                            status.native_new_round_transition_release_generation)
                        .uinteger(
                            "native_new_round_baseline_save_generation",
                            status.native_new_round_baseline_save_generation)
                        .hex(
                            "native_new_round_baseline_live_current",
                            status.native_new_round_baseline_live_current)
                        .hex(
                            "native_new_round_baseline_live_queued",
                            status.native_new_round_baseline_live_queued)
                        .uinteger(
                            "native_new_round_baseline_live_transition",
                            status.native_new_round_baseline_live_transition)
                        .hex(
                            "native_new_round_baseline_saved_current",
                            status.native_new_round_baseline_saved_current)
                        .hex(
                            "native_new_round_baseline_saved_queued",
                            status.native_new_round_baseline_saved_queued)
                        .uinteger(
                            "native_new_round_baseline_saved_transition",
                            status.native_new_round_baseline_saved_transition)
                        .boolean(
                            "native_new_round_baseline_save_verified",
                            status.native_new_round_baseline_save_verified)
                        .hex(
                            "native_terminal_world_current_before",
                            status.native_terminal_world_current_before)
                        .hex(
                            "native_terminal_world_queued_before",
                            status.native_terminal_world_queued_before)
                        .hex(
                            "native_terminal_world_current_after",
                            status.native_terminal_world_current_after)
                        .hex(
                            "native_terminal_world_queued_after",
                            status.native_terminal_world_queued_after)
                        .boolean(
                            "native_round_over_commit_observation_pending",
                            status.native_round_over_commit_observation_pending)
                        .uinteger("stock_round_transition_result_ticks",
                            status.stock_round_transition_result_ticks)
                        .uinteger("stock_round_transition_wait_ticks",
                            status.stock_round_transition_wait_ticks)
                        .boolean("stock_round_terminal_quiesced",
                            status.stock_round_terminal_quiesced)
                        .boolean("stock_round_terminal_barrier_ready",
                            status.stock_round_terminal_barrier_ready)
                        .uinteger("stock_round_terminal_local_stage",
                            status.stock_round_terminal_local_stage)
                        .uinteger("stock_round_terminal_peer_stage",
                            status.stock_round_terminal_peer_stage)
                        .uinteger("stock_round_terminal_local_tail_start",
                            status.stock_round_terminal_local_tail_start)
                        .uinteger("stock_round_terminal_peer_tail_start",
                            status.stock_round_terminal_peer_tail_start)
                        .uinteger("stock_round_terminal_resend_start",
                            status.stock_round_terminal_resend_start)
                        .uinteger("stock_round_terminal_confirmed_frame",
                            status.stock_round_terminal_confirmed_frame)
                        .hex("stock_round_terminal_canonical_hash",
                            status.stock_round_terminal_canonical_hash)
                        .uinteger("stock_round_terminal_restores",
                            status.stock_round_terminal_restores)
                        .uinteger("stock_round_terminal_messages_sent",
                            status.stock_round_terminal_messages_sent)
                        .uinteger("stock_round_terminal_messages_received",
                            status.stock_round_terminal_messages_received)
                        .uinteger(
                            "stock_round_terminal_accepted_republishes",
                            status.stock_round_terminal_accepted_republishes)
                        .uinteger(
                            "stock_round_terminal_accepted_republish_deferrals",
                            status
                                .stock_round_terminal_accepted_republish_deferrals)
                        .uinteger(
                            "stock_round_terminal_transport_drain_calls",
                            status.stock_round_terminal_transport_drain_calls)
                        .uinteger(
                            "stock_round_terminal_late_gekko_discards",
                            status.stock_round_terminal_late_gekko_discards)
                        .uinteger(
                            "stock_round_terminal_drain_budget_exhaustions",
                            status
                                .stock_round_terminal_drain_budget_exhaustions)
                        .uinteger("stock_round_terminal_proposals_sent",
                            status.stock_round_terminal_proposals_sent)
                        .uinteger("stock_round_terminal_proposals_received",
                            status.stock_round_terminal_proposals_received)
                        .uinteger("stock_round_terminal_proposals_adopted",
                            status.stock_round_terminal_proposals_adopted)
                        .uinteger("stock_round_terminal_proposals_agreed",
                            status.stock_round_terminal_proposals_agreed)
                        .uinteger(
                            "stock_round_terminal_last_initial_proposal_frame",
                            status
                                .stock_round_terminal_last_initial_proposal_frame)
                        .hex(
                            "stock_round_terminal_last_initial_proposal_hash",
                            status
                                .stock_round_terminal_last_initial_proposal_hash)
                        .boolean(
                            "stock_round_terminal_last_proposal_adopted",
                            status.stock_round_terminal_last_proposal_adopted)
                        .uinteger(
                            "stock_round_terminal_last_agreed_frame",
                            status.stock_round_terminal_last_agreed_frame)
                        .uinteger(
                            "stock_round_terminal_last_agreed_ordinal",
                            status.stock_round_terminal_last_agreed_ordinal)
                        .hex(
                            "stock_round_terminal_last_agreed_pair_epoch",
                            status.stock_round_terminal_last_agreed_pair_epoch)
                        .hex("stock_round_terminal_last_agreed_hash",
                            status.stock_round_terminal_last_agreed_hash)
                        .boolean(
                            "stock_round_terminal_candidate_pair_matched",
                            status.stock_round_terminal_candidate_pair_matched)
                        .boolean(
                            "stock_round_terminal_candidate_handle_valid",
                            status.stock_round_terminal_candidate_handle_valid)
                        .uinteger("stock_round_terminal_candidate_frame",
                            status.stock_round_terminal_candidate_frame)
                        .uinteger(
                            "stock_round_terminal_candidate_generation",
                            status.stock_round_terminal_candidate_generation)
                        .string(
                            "stock_round_terminal_checkpoint_validation_stage",
                            status
                                .stock_round_terminal_checkpoint_validation_stage)
                        .string(
                            "stock_round_terminal_checkpoint_integrity_failure",
                            status
                                .stock_round_terminal_checkpoint_integrity_failure)
                        .uinteger(
                            "stock_round_terminal_unretained_matches",
                            status.stock_round_terminal_unretained_matches)
                        .uinteger(
                            "stock_round_terminal_candidates_sent",
                            status.stock_round_terminal_candidates_sent)
                        .uinteger(
                            "stock_round_terminal_candidates_received",
                            status.stock_round_terminal_candidates_received)
                        .uinteger(
                            "stock_round_terminal_candidate_matches",
                            status.stock_round_terminal_candidate_matches)
                        .uinteger(
                            "stock_round_terminal_candidate_mismatches",
                            status.stock_round_terminal_candidate_mismatches)
                        .uinteger(
                            "stock_round_terminal_candidate_replacements",
                            status.stock_round_terminal_candidate_replacements)
                        .boolean("stock_round_transition_terminal_seen",
                            status.stock_round_transition_terminal_seen)
                        .uinteger("stock_round_transition_last_state",
                            status.stock_round_transition_last_state)
                        .uinteger("stock_round_wins_0",
                            status.stock_round_wins_0)
                        .uinteger("stock_round_wins_1",
                            status.stock_round_wins_1)
                        .uinteger("stock_rounds_to_win_0",
                            status.stock_rounds_to_win_0)
                        .uinteger("stock_rounds_to_win_1",
                            status.stock_rounds_to_win_1)
                        .uinteger("round_result_flow_state",
                            status.round_result_flow_state)
                        .boolean("native_online_session_active",
                            status.native_online_session_active)
                        .integer("native_round_state_sequence_count",
                            status.native_round_state_sequence_count)
                        .integer("native_round_state_sequence_capacity",
                            status.native_round_state_sequence_capacity)
                        .hex("world_mode_current",
                            status.world_mode_current)
                        .hex("world_mode_queued",
                            status.world_mode_queued)
                        .uinteger("world_mode_transition_completed",
                            status.world_mode_transition_completed)
                        .uinteger("world_mode_scratch_state",
                            status.world_mode_scratch_state)
                        .integer("world_mode_subdriver_state",
                            status.world_mode_subdriver_state)
                        .uinteger("round_result_mode_tick_limit",
                            status.round_result_mode_tick_limit)
                        .uinteger("round_result_mode_advance_frame",
                            status.round_result_mode_advance_frame)
                        .uinteger("round_result_mode_frame_counter",
                            status.round_result_mode_frame_counter)
                        .uinteger("round_result_mode_initialized",
                            status.round_result_mode_initialized)
                        .uinteger("round_result_mode_wait_complete",
                            status.round_result_mode_wait_complete)
                        .uinteger("round_result_mode_cinematic_triggered",
                            status.round_result_mode_cinematic_triggered)
                        .uinteger("new_round_mode_frame",
                            status.new_round_mode_frame)
                        .uinteger("new_round_mode_timer",
                            status.new_round_mode_timer)
                        .integer("new_round_mode_phase",
                            status.new_round_mode_phase)
                        .uinteger("state_capture_nanoseconds",
                            status.state_capture_nanoseconds)
                        .uinteger("state_capture_calls",
                            status.state_capture_calls)
                        .uinteger("explicit_capture_nanoseconds",
                            status.explicit_capture_nanoseconds)
                        .uinteger("hgcpu_capture_nanoseconds",
                            status.hgcpu_capture_nanoseconds)
                        .uinteger("palette_variant_capture_nanoseconds",
                            status.palette_variant_capture_nanoseconds)
                        .uinteger("explicit_cleanup_nanoseconds",
                            status.explicit_cleanup_nanoseconds)
                        .uinteger("stage_capture_nanoseconds",
                            status.stage_capture_nanoseconds)
                        .uinteger("wind_capture_nanoseconds",
                            status.wind_capture_nanoseconds)
                        .uinteger("stage_wind_native_tick_calls",
                            status.stage_wind_native_tick_calls)
                        .uinteger("stage_wind_native_spawn_calls",
                            status.stage_wind_native_spawn_calls)
                        .uinteger("stage_wind_pool_allocations",
                            status.stage_wind_pool_allocations)
                        .uinteger("stage_wind_pool_frees",
                            status.stage_wind_pool_frees)
                        .uinteger("stage_wind_external_deferred_releases",
                            status.stage_wind_external_deferred_releases)
                        .uinteger("stage_wind_pool_peak",
                            status.stage_wind_pool_peak)
                        .uinteger("capture_finalize_nanoseconds",
                            status.capture_finalize_nanoseconds)
                        .uinteger("hgcpu_emergency_capture_nanoseconds",
                            status.hgcpu_emergency_capture_nanoseconds)
                        .uinteger("hgcpu_native_capture_nanoseconds",
                            status.hgcpu_native_capture_nanoseconds)
                        .uinteger("hgcpu_emergency_restore_nanoseconds",
                            status.hgcpu_emergency_restore_nanoseconds)
                        .uinteger("hgcpu_khit_capture_nanoseconds",
                            status.hgcpu_khit_capture_nanoseconds)
                        .uinteger("hgcpu_timer_capture_nanoseconds",
                            status.hgcpu_timer_capture_nanoseconds)
                        .uinteger("hgcpu_hash_finalize_nanoseconds",
                            status.hgcpu_hash_finalize_nanoseconds)
                        .uinteger("restore_nanoseconds",
                            status.restore_nanoseconds)
                        .uinteger("restore_calls", status.restore_calls)
                        .uinteger("verification_nanoseconds",
                            status.verification_nanoseconds)
                        .uinteger("verification_calls",
                            status.verification_calls)
                        .uinteger("owned_tick_nanoseconds",
                            status.owned_tick_nanoseconds)
                        .uinteger("owned_tick_calls",
                            status.owned_tick_calls)
                        .uinteger("native_tick_nanoseconds",
                            status.native_tick_nanoseconds)
                        .uinteger("native_tick_calls",
                            status.native_tick_calls)
                        .uinteger("transition_capture_nanoseconds",
                            status.transition_capture_nanoseconds)
                        .uinteger("transition_capture_calls",
                            status.transition_capture_calls)
                        .uinteger("production_simulation_tick_calls",
                            status.production_simulation_tick_calls)
                        .uinteger("owned_simulation_organic_entries",
                            status.owned_simulation_organic_entries)
                        .uinteger("owned_simulation_service_checks",
                            status.owned_simulation_service_checks)
                        .uinteger(
                            "owned_simulation_service_fallback_activations",
                            status.
                                owned_simulation_service_fallback_activations)
                        .uinteger("owned_simulation_service_calls",
                            status.owned_simulation_service_calls)
                        .uinteger("owned_simulation_service_missed_ticks",
                            status.owned_simulation_service_missed_ticks)
                        .integer("confirmed_frame",
                            status.confirmed_frame.valid
                                ? static_cast<int64_t>(
                                    status.confirmed_frame.value) : -1)
                        .integer("corrected_frame",
                            status.corrected_frame.valid
                                ? static_cast<int64_t>(
                                    status.corrected_frame.value) : -1)
                        .hex("confirmed_canonical_hash",
                            status.confirmed_canonical_hash)
                        .boolean("baseline_restore_verified",
                            status.baseline_restore_verified)
                        .boolean("prediction_restore_verified",
                            status.prediction_restore_verified)
                        .boolean("final_restore_verified",
                            status.final_restore_verified)
                        .hex("last_restore_expected_hash",
                            status.last_restore_expected_hash)
                        .hex("last_restore_observed_hash",
                            status.last_restore_observed_hash)
                        .uinteger("presentation_queued",
                            status.presentation_queued)
                        .uinteger("presentation_discarded",
                            status.presentation_discarded)
                        .uinteger("presentation_committed",
                            status.presentation_committed)
                        .uinteger("audio_effects_queued",
                            status.audio_effects_queued)
                        .uinteger("audio_effects_discarded",
                            status.audio_effects_discarded)
                        .uinteger("audio_effects_committed",
                            status.audio_effects_committed)
                        .hex("audio_effects_committed_digest",
                            status.audio_effects_committed_digest)
                        .uinteger("vfx_effects_queued",
                            status.vfx_effects_queued)
                        .uinteger("vfx_effects_discarded",
                            status.vfx_effects_discarded)
                        .uinteger("vfx_effects_committed",
                            status.vfx_effects_committed)
                        .hex("vfx_effects_committed_digest",
                            status.vfx_effects_committed_digest)
                        .uinteger("camera_effects_queued",
                            status.camera_effects_queued)
                        .uinteger("camera_effects_discarded",
                            status.camera_effects_discarded)
                        .uinteger("camera_effects_committed",
                            status.camera_effects_committed)
                        .hex("camera_effects_committed_digest",
                            status.camera_effects_committed_digest)
                        .uinteger("transitions_queued",
                            status.transitions_queued)
                        .uinteger("transitions_discarded",
                            status.transitions_discarded)
                        .uinteger("transitions_committed",
                            status.transitions_committed)
                        .hex("transitions_committed_digest",
                            status.transitions_committed_digest)
                        .uinteger("transition_hit_committed",
                            status.transition_hit_committed)
                        .uinteger("transition_damage_committed",
                            status.transition_damage_committed)
                        .uinteger("transition_meter_committed",
                            status.transition_meter_committed)
                        .uinteger("transition_round_committed",
                            status.transition_round_committed)
                        .uinteger("model_reconciliation_queued",
                            status.model_reconciliation_queued)
                        .uinteger("model_reconciliation_discarded",
                            status.model_reconciliation_discarded)
                        .uinteger("model_reconciliation_committed",
                            status.model_reconciliation_committed)
                        .boolean("model_reconciliation_after_load",
                            status.model_reconciliation_after_load)
                        .uinteger("stage_reconciliation_queued",
                            status.stage_reconciliation_queued)
                        .uinteger("stage_reconciliation_discarded",
                            status.stage_reconciliation_discarded)
                        .uinteger("stage_reconciliation_committed",
                            status.stage_reconciliation_committed)
                        .hex("stage_reconciliation_digest",
                            status.stage_reconciliation_digest)
                        .uinteger("stage_breakable_actor_count",
                            status.stage_breakable_actor_count)
                        .boolean("stage_reconciliation_after_load",
                            status.stage_reconciliation_after_load)
                        .string("stage_reconciliation_failure",
                            status.stage_reconciliation_failure
                                ? status.stage_reconciliation_failure
                                : "unknown")
                        .uinteger("coverage_hgcpu",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::HgCpuState)])
                        .uinteger("coverage_khit",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::KHitState)])
                        .uinteger("coverage_session_baseline",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::SessionBaseline)])
                        .uinteger("coverage_round_identity",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::RoundIdentity)])
                        .uinteger("coverage_native_round_state_queue",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::
                                    NativeRoundStateQueue)])
                        .uinteger("coverage_native_simulation_state",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::
                                    NativeSimulationState)])
                        .uinteger("coverage_palette_variant_state",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::
                                    PaletteVariantState)])
                        .uinteger("coverage_explicit_state",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::ExplicitState)])
                        .uinteger("coverage_excluded_state",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::ExcludedState)])
                        .uinteger("coverage_breakable_scalars",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::BreakableScalars)])
                        .uinteger("coverage_stage_wind",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::StageWind)])
                        .uinteger("coverage_stage_identity",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::StageIdentity)])
                        .uinteger("coverage_presentation_dispatch",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::PresentationDispatch)])
                        .uinteger("coverage_historical_camera",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::HistoricalCamera)])
                        .uinteger("coverage_breakable_presentation",
                            status.coverage_invocations[static_cast<size_t>(
                                RollbackCoverageCapabilityId::BreakablePresentation)])
                        .boolean("presentation_root_publish_valid",
                            status.presentation_root_publish_valid)
                        .boolean("presentation_accounting_consistent",
                            status.presentation_accounting_consistent)
                        .string("network_profile",
                            RollbackNetworkProfileName(
                                static_cast<RollbackNetworkProfileKind>(
                                    status.network_profile)))
                        .uinteger("fault_seed", status.fault_seed)
                        .uinteger("fault_submitted",
                            status.fault_packets_submitted)
                        .uinteger("fault_queued",
                            status.fault_packets_queued)
                        .uinteger("fault_delivered",
                            status.fault_packets_delivered)
                        .uinteger("fault_dropped",
                            status.fault_packets_dropped)
                        .uinteger("fault_duplicated",
                            status.fault_packets_duplicated)
                        .uinteger("fault_reordered",
                            status.fault_packets_reordered)
                        .uinteger("fault_corrupted",
                            status.fault_packets_corrupted)
                        .uinteger("fault_spiked",
                            status.fault_packets_spiked)
                        .uinteger("fault_burst_dropped",
                            status.fault_packets_burst_dropped)
                        .uinteger("test_worker_stalls_started",
                            status.test_worker_stalls_started)
                        .uinteger("test_worker_stalls_completed",
                            status.test_worker_stalls_completed)
                        .uinteger("test_worker_stall_actual_ms",
                            status.test_worker_stall_actual_ms)
                        .uinteger("fault_queue_overflows",
                            status.fault_queue_overflows)
                        .uinteger("network_packets_sent",
                            status.network_packets_sent)
                        .uinteger("network_packets_received",
                            status.network_packets_received)
                        .uinteger("network_packets_authenticated",
                            status.network_packets_authenticated)
                        .uinteger("network_packets_rejected",
                            status.network_packets_rejected)
                        .uinteger("network_packets_decode_rejected",
                            status.network_packets_decode_rejected)
                        .uinteger("network_packets_route_rejected",
                            status.network_packets_route_rejected)
                        .uinteger("network_packets_replay_rejected",
                            status.network_packets_replay_rejected)
                        .uinteger("network_queue_overflows",
                            status.network_queue_overflows)
                        .uinteger("network_redundant_enqueue_deferrals",
                            status.network_redundant_enqueue_deferrals)
                        .uinteger("network_failure",
                            status.network_failure);
                    fields
                        .uinteger("peer_liveness_checks",
                            status.peer_liveness_checks)
                        .uinteger(
                            "peer_liveness_last_authenticated_packets",
                            status
                                .peer_liveness_last_authenticated_packets)
                        .uinteger("peer_liveness_last_progress_us",
                            status.peer_liveness_last_progress_us)
                        .uinteger("peer_liveness_stalled_observations",
                            status.peer_liveness_stalled_observations);
                    for (size_t route = 0;
                         route < kRollbackVfxRouteCount; ++route)
                    {
                        char queued_key[40] {};
                        char committed_key[40] {};
                        char digest_key[48] {};
                        const uint16_t offset =
                            kRollbackVfxRoutes[route].vtable_offset;
                        if (!FormatRollbackVfxRouteTraceKey(
                                queued_key, sizeof(queued_key), offset,
                                "queued")
                            || !FormatRollbackVfxRouteTraceKey(
                                committed_key, sizeof(committed_key), offset,
                                "committed")
                            || !FormatRollbackVfxRouteTraceKey(
                                digest_key, sizeof(digest_key), offset,
                                "committed_digest"))
                        {
                            continue;
                        }
                        fields
                            .uinteger(queued_key,
                                status.vfx_route_queued[route])
                            .uinteger(committed_key,
                                status.vfx_route_committed[route])
                            .hex(digest_key,
                                status.vfx_route_committed_digest[route]);
                    }
                    for (size_t player = 0; player < 2; ++player)
                    {
                        for (size_t chunk = 0;
                             chunk < kRollbackHgCpuPeerCharaChunkCount;
                             ++chunk)
                        {
                            char local_key[64] {};
                            char peer_key[64] {};
                            std::snprintf(local_key, sizeof(local_key),
                                "mismatch_local_hgcpu_chara_%zu_chunk_%zu",
                                player, chunk);
                            std::snprintf(peer_key, sizeof(peer_key),
                                "mismatch_peer_hgcpu_chara_%zu_chunk_%zu",
                                player, chunk);
                            fields.hex(local_key,
                                status.mismatch_local_hgcpu_peer
                                    .chara_chunk_hash[player][chunk]);
                            fields.hex(peer_key,
                                status.mismatch_peer_hgcpu_peer
                                    .chara_chunk_hash[player][chunk]);
                        }
                    }
                    if ((status.mismatch_hgcpu_peer_mask
                            & (kRollbackHgCpuPeerMismatchKHit0
                                | kRollbackHgCpuPeerMismatchKHit1)) != 0)
                    {
                        for (size_t player = 0; player < 2; ++player)
                        {
                            for (size_t list = 0;
                                 list < kRollbackHgCpuKHitListCount; ++list)
                            {
                                char local_key[80] {};
                                char peer_key[80] {};
                                std::snprintf(local_key, sizeof(local_key),
                                    "mismatch_local_khit_p%zu_l%zu_full",
                                    player, list);
                                std::snprintf(peer_key, sizeof(peer_key),
                                    "mismatch_peer_khit_p%zu_l%zu_full",
                                    player, list);
                                fields.hex(local_key,
                                    status.mismatch_local_hgcpu_peer
                                        .khit_list_hash[player][list]);
                                fields.hex(peer_key,
                                    status.mismatch_peer_hgcpu_peer
                                        .khit_list_hash[player][list]);
                                std::snprintf(local_key,
                                    sizeof(local_key),
                                    "mismatch_local_khit_p%zu_l%zu_source_matrix",
                                    player, list);
                                std::snprintf(peer_key,
                                    sizeof(peer_key),
                                    "mismatch_peer_khit_p%zu_l%zu_source_matrix",
                                    player, list);
                                fields.hex(local_key,
                                    status.mismatch_local_hgcpu_peer
                                        .khit_source_matrix_hash[player][list]);
                                fields.hex(peer_key,
                                    status.mismatch_peer_hgcpu_peer
                                        .khit_source_matrix_hash[player][list]);
                                std::snprintf(local_key,
                                    sizeof(local_key),
                                    "mismatch_local_khit_p%zu_l%zu_bone_min",
                                    player, list);
                                std::snprintf(peer_key,
                                    sizeof(peer_key),
                                    "mismatch_peer_khit_p%zu_l%zu_bone_min",
                                    player, list);
                                fields.uinteger(local_key,
                                    status.mismatch_local_hgcpu_peer
                                        .khit_source_bone_min[player][list]);
                                fields.uinteger(peer_key,
                                    status.mismatch_peer_hgcpu_peer
                                        .khit_source_bone_min[player][list]);
                                std::snprintf(local_key,
                                    sizeof(local_key),
                                    "mismatch_local_khit_p%zu_l%zu_bone_max",
                                    player, list);
                                std::snprintf(peer_key,
                                    sizeof(peer_key),
                                    "mismatch_peer_khit_p%zu_l%zu_bone_max",
                                    player, list);
                                fields.uinteger(local_key,
                                    status.mismatch_local_hgcpu_peer
                                        .khit_source_bone_max[player][list]);
                                fields.uinteger(peer_key,
                                    status.mismatch_peer_hgcpu_peer
                                        .khit_source_bone_max[player][list]);
                                for (size_t lane = 0;
                                     lane < kRollbackHgCpuKHitPayloadLaneCount;
                                     ++lane)
                                {
                                    std::snprintf(local_key,
                                        sizeof(local_key),
                                        "mismatch_local_khit_p%zu_l%zu_lane%zu",
                                        player, list, lane);
                                    std::snprintf(peer_key,
                                        sizeof(peer_key),
                                        "mismatch_peer_khit_p%zu_l%zu_lane%zu",
                                        player, list, lane);
                                    fields.hex(local_key,
                                        status.mismatch_local_hgcpu_peer
                                            .khit_payload_lane_hash
                                                [player][list][lane]);
                                    fields.hex(peer_key,
                                        status.mismatch_peer_hgcpu_peer
                                            .khit_payload_lane_hash
                                                [player][list][lane]);
                                }
                            }
                        }
                    }
                    for (size_t player = 0; player < 2; ++player)
                    {
                        for (size_t partition = 0;
                             partition
                                 < kRollbackHgCpuCurrentMatrixPartitionCount;
                             ++partition)
                        {
                            const uint64_t local_partition =
                                status.mismatch_local_hgcpu_peer
                                    .motion_current_partition_hash
                                        [player][partition];
                            const uint64_t peer_partition =
                                status.mismatch_peer_hgcpu_peer
                                    .motion_current_partition_hash
                                        [player][partition];
                            if (local_partition == peer_partition) continue;
                            char local_key[80] {};
                            char peer_key[80] {};
                            std::snprintf(local_key, sizeof(local_key),
                                "mismatch_local_motion_current_p%zu_part%zu",
                                player, partition);
                            std::snprintf(peer_key, sizeof(peer_key),
                                "mismatch_peer_motion_current_p%zu_part%zu",
                                player, partition);
                            fields.hex(local_key, local_partition);
                            fields.hex(peer_key, peer_partition);
                        }
                    }
                    observe_qualification_status(status);
                    if (m_config.qualification_enabled())
                        append_qualification_tags(fields);
                    ReplayDebugTrace::instance().event(
                        "rollback_production_status", fields);
                    m_production_last_trace_tick = tick;
                    m_production_last_trace_state = status.state;
                }
                // The stock driver owns the only consecutive-observation
                // counter. Unknown scene identities and renewed battle
                // observations reset it to zero.
                m_production_non_pvp_observations =
                    m_cleanup_handoff_active
                    ? m_p2p_harness
                        .stock_cleanup_out_of_battle_observations()
                    : 0;
                if (tick == 1 || (tick % 600) == 0)
                {
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[RollbackProduction] state={} failure={} "
                        "build={:#x} expected_build={:#x} schema={:#x} "
                        "expected_schema={:#x} epoch={} saves={} loads={} "
                        "advances={} rollback_advances={} pair_accepts={} "
                        "mode={} native_input={} gekko_slot={} "
                        "launch_ready={}\n"),
                        static_cast<unsigned>(status.state),
                        RC::to_generic_string(std::string(
                            status.failure ? status.failure : "?")),
                        status.executable_id,
                        m_config.production.expected_build_id,
                        status.schema_id,
                        m_config.production.expected_schema_id,
                        status.epoch,
                        status.saves,
                        status.loads,
                        status.advances,
                        status.rollback_advances,
                        status.pair_accepts,
                        status.lifecycle_mode,
                        status.native_input_source_slot,
                        status.local_player_slot,
                        status.launch_barrier_ready ? 1 : 0);
                }
                if (status.lobby_return_requested
                    && m_cleanup_handoff_active)
                {
                    const bool dispatched = m_p2p_harness
                        .stock_cleanup_exit_dispatched();
                    const bool succeeded = m_p2p_harness
                        .stock_cleanup_exit_succeeded();
                    if (dispatched)
                        production.record_lobby_return_dispatch(succeeded);
                }
                if (status.lobby_return_requested
                    && m_production_non_pvp_observations >= 3)
                {
                    // Three independently captured non-PVP observations are
                    // the teardown proof. A console command merely being
                    // accepted is diagnostic and never releases the frozen
                    // boundary by itself.
                    if (production.authorize_out_of_battle_shutdown())
                    {
                        emit_qualification_terminal();
                        production.shutdown();
                    }
                    else
                        production.request_fail_closed(
                            "out-of-battle-shutdown-authorization-failed");
                    if (m_pending_config)
                    {
                        RollbackLabConfig pending =
                            std::move(*m_pending_config);
                        m_pending_config.reset();
                        configure(std::move(pending));
                        return;
                    }
                    m_cleanup_handoff_active = false;
                    m_config.production.enabled = false;
                    m_config.enabled = false;
                }
                return;
            }
            if (m_config.test_case == RollbackLabCase::ReplayForkLab)
            {
                RollbackReplayForkRuntime::instance().service_game_thread(
                    m_manifest);
                return;
            }

            if (m_config.test_case == RollbackLabCase::SnapshotRoundTrip
                && !m_snapshot_probe_ran)
            {
                m_snapshot_probe_ran = true;
                m_last_snapshot_probe = run_snapshot_roundtrip_probe();
                RollbackDiag::emit_snapshot_roundtrip(
                    m_last_snapshot_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::SnapshotRoundTrip
                && !m_hgcpu_probe_ran)
            {
                m_last_hgcpu_probe = run_hgcpu_roundtrip_probe();
                if (m_last_hgcpu_probe.capture.context_ready
                    || !m_last_hgcpu_probe.capture.failure
                    || std::strcmp(
                        m_last_hgcpu_probe.capture.failure,
                        "battle-context-not-ready") != 0)
                {
                    m_hgcpu_probe_ran = true;
                    RollbackDiag::emit_hgcpu_roundtrip(
                        m_last_hgcpu_probe, m_config);
                }
                else if (tick == 1 || (tick % 600) == 0)
                {
                    RollbackDiag::emit_hgcpu_roundtrip(
                        m_last_hgcpu_probe, m_config);
                }
            }
            if ((m_config.test_case == RollbackLabCase::BaselineOracle
                 || m_config.test_case == RollbackLabCase::DelayedInputCorrection)
                && !m_resim_probe_ran)
            {
                m_last_resim_probe = run_resim_window_probe(
                    m_config.rollback_window,
                    m_config.test_case
                    == RollbackLabCase::DelayedInputCorrection);
                if (m_last_resim_probe.context_ready
                    || std::strcmp(
                        m_last_resim_probe.failure,
                        "battle-context-not-ready") != 0)
                {
                    m_resim_probe_ran = true;
                    RollbackDiag::emit_resim_window(
                        m_last_resim_probe, m_config);
                }
                else if (tick == 1 || (tick % 600) == 0)
                {
                    RollbackDiag::emit_resim_window(
                        m_last_resim_probe, m_config);
                }
            }
            if (m_config.test_case == RollbackLabCase::ResimMatrix
                && !m_resim_probe_ran)
            {
                if (m_manifest.epoch.chara[0] == 0
                    || m_manifest.epoch.chara[1] == 0
                    || !m_manifest.epoch.active_pvp())
                {
                    if (tick == 1 || (tick % 600) == 0)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[RollbackLab] resim_matrix "
                            "waiting_for_context=1\n"));
                    }
                    return;
                }

                bool context_wait = false;
                bool all_ok = true;
                static constexpr uint32_t kWindows[] = {1, 2, 8, 15, 60};
                for (uint32_t window : kWindows)
                {
                    RollbackLabConfig emit_cfg = m_config;
                    emit_cfg.rollback_window = window;

                    emit_cfg.test_case = RollbackLabCase::BaselineOracle;
                    RollbackResimWindowReport baseline =
                        run_resim_window_probe(window, false);
                    if (!baseline.context_ready
                        && std::strcmp(
                            baseline.failure,
                            "battle-context-not-ready") == 0)
                    {
                        context_wait = true;
                        break;
                    }
                    RollbackDiag::emit_resim_window(baseline, emit_cfg);
                    all_ok = all_ok && baseline.ok;

                    emit_cfg.test_case =
                        RollbackLabCase::DelayedInputCorrection;
                    RollbackResimWindowReport delayed =
                        run_resim_window_probe(window, true);
                    if (!delayed.context_ready
                        && std::strcmp(
                            delayed.failure,
                            "battle-context-not-ready") == 0)
                    {
                        context_wait = true;
                        break;
                    }
                    RollbackDiag::emit_resim_window(delayed, emit_cfg);
                    all_ok = all_ok && delayed.ok;
                }

                if (!context_wait)
                {
                    m_resim_probe_ran = true;
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[RollbackLab] resim_matrix ok={} windows=1,2,8,15,60 "
                        "cases=baseline-oracle,delayed-input\n"),
                        all_ok ? 1 : 0);
                }
                else if (tick == 1 || (tick % 600) == 0)
                {
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[RollbackLab] resim_matrix waiting_for_context=1\n"));
                }
            }
            if (m_config.test_case == RollbackLabCase::CacheOwnershipTrace
                && !m_cache_probe_ran)
            {
                if (m_manifest.epoch.chara[0] == 0
                    || m_manifest.epoch.chara[1] == 0
                    || !m_manifest.epoch.active_pvp())
                {
                    if (tick == 1 || (tick % 600) == 0)
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[RollbackLab] cache_ownership "
                            "waiting_for_context=1\n"));
                    }
                    return;
                }

                m_last_cache_probe = run_cache_ownership_probe();
                const bool waiting_for_context =
                    !m_last_cache_probe.context_ready
                    && m_last_cache_probe.failure
                    && (std::strcmp(
                            m_last_cache_probe.failure,
                            "battle-manager-not-found") == 0
                        || std::strcmp(
                            m_last_cache_probe.failure,
                            "input-log-not-found") == 0
                        || std::strcmp(
                            m_last_cache_probe.failure,
                            "battle-context-not-ready") == 0
                        || std::strcmp(
                            m_last_cache_probe.failure,
                            "input-log-warmup-pending") == 0
                        || std::strcmp(
                            m_last_cache_probe.failure,
                            "lifecycle-epoch-not-current") == 0);
                if (!waiting_for_context)
                {
                    m_cache_probe_ran = true;
                    RollbackDiag::emit_cache_ownership(
                        m_last_cache_probe, m_config);
                }
                else if (tick == 1 || (tick % 600) == 0)
                {
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[RollbackLab] cache_ownership "
                        "waiting_for_context=1 failure={}\n"),
                        RC::to_generic_string(std::string(
                            m_last_cache_probe.failure
                                ? m_last_cache_probe.failure
                                : "?")));
                }
            }
            if (m_config.test_case == RollbackLabCase::OnlineSessionSelfTest
                && !m_online_session_probe_ran)
            {
                m_online_session_probe_ran = true;
                m_last_online_session_probe =
                    RunRollbackOnlineSessionSelfTest();
                RollbackDiag::emit_online_session_selftest(
                    m_last_online_session_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::LiveTransportSelfTest
                && !m_live_transport_probe_ran)
            {
                m_live_transport_probe_ran = true;
                m_last_live_transport_probe =
                    RunRollbackLiveTransportQueueSelfTest();
                RollbackDiag::emit_live_transport_selftest(
                    m_last_live_transport_probe, m_config);
            }
            if (m_config.test_case
                    == RollbackLabCase::LivePeerPipelineSelfTest
                && !m_live_peer_pipeline_probe_ran)
            {
                m_live_peer_pipeline_probe_ran = true;
                m_last_live_peer_pipeline_probe =
                    RunRollbackLivePeerPipelineSelfTest();
                RollbackDiag::emit_live_peer_pipeline_selftest(
                    m_last_live_peer_pipeline_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::EndToEndSelfTest
                && !m_end_to_end_probe_ran)
            {
                m_end_to_end_probe_ran = true;
                m_last_end_to_end_probe = RunRollbackEndToEndSelfTest();
                RollbackDiag::emit_end_to_end_selftest(
                    m_last_end_to_end_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::LiveActivationSelfTest
                && !m_live_activation_probe_ran)
            {
                m_live_activation_probe_ran = true;
                m_last_live_activation_probe =
                    RunRollbackLiveActivationSelfTest();
                RollbackDiag::emit_live_activation_selftest(
                    m_last_live_activation_probe, m_config);
            }
            if (m_config.test_case
                    == RollbackLabCase::LiveActivationExecutorSelfTest
                && !m_live_activation_executor_probe_ran)
            {
                m_live_activation_executor_probe_ran = true;
                m_last_live_activation_executor_probe =
                    RunRollbackLiveActivationExecutorSelfTest();
                RollbackDiag::emit_live_activation_executor_selftest(
                    m_last_live_activation_executor_probe, m_config);
            }
            if (m_config.test_case
                    == RollbackLabCase::GekkoGameplayInputSelfTest
                && !m_gekko_gameplay_input_probe_ran)
            {
                m_gekko_gameplay_input_probe_ran = true;
                m_last_gekko_gameplay_input_probe =
                    RunRollbackGekkoGameplayInputBridgeSelfTest();
                RollbackDiag::emit_gekko_gameplay_input_selftest(
                    m_last_gekko_gameplay_input_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::GekkoSessionSelfTest
                && !m_gekko_session_probe_ran)
            {
                m_gekko_session_probe_ran = true;
                m_last_gekko_session_probe =
                    RunRollbackGekkoSessionSelfTest();
                RollbackDiag::emit_gekko_session_selftest(
                    m_last_gekko_session_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::GekkoAdapterSelfTest
                && !m_gekko_adapter_probe_ran)
            {
                m_gekko_adapter_probe_ran = true;
                m_last_gekko_adapter_probe =
                    RunRollbackGekkoAdapterSelfTest();
                RollbackDiag::emit_gekko_adapter_selftest(
                    m_last_gekko_adapter_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::GekkoUdpSelfTest
                && !m_gekko_udp_probe_ran)
            {
                m_gekko_udp_probe_ran = true;
                m_last_gekko_udp_probe =
                    RunRollbackGekkoUdpAdapterSelfTest();
                RollbackDiag::emit_gekko_udp_selftest(
                    m_last_gekko_udp_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::StockTransportSelfTest
                && !m_stock_transport_probe_ran)
            {
                m_stock_transport_probe_ran = true;
                m_last_stock_transport_probe =
                    RunRollbackStockTransportSurfaceSelfTest();
                RollbackDiag::emit_stock_transport_selftest(
                    m_last_stock_transport_probe, m_config);
            }
            if (m_config.test_case == RollbackLabCase::StockTransportObserve)
            {
                if (!RollbackStockTransportObserveHook::instance().installed())
                {
                    if (RollbackStockTransportObserveHook::instance().install())
                    {
                        RollbackStockTransportObserveHook::instance()
                            .begin_trace();
                    }
                }
                m_last_stock_transport_observe_probe =
                    RollbackStockTransportObserveHook::instance().report();
                const uint64_t total =
                    m_last_stock_transport_observe_probe.total_observed_calls;
                if (tick == 1
                    || total != m_last_stock_transport_observe_total
                    || (tick % 600) == 0)
                {
                    RollbackDiag::emit_stock_transport_observe(
                        m_last_stock_transport_observe_probe, m_config);
                    m_last_stock_transport_observe_total = total;
                }
            }
            if (m_config.test_case == RollbackLabCase::LiveOnlineCapture)
            {
                if (!RollbackStockTransportObserveHook::instance().installed())
                {
                    if (RollbackStockTransportObserveHook::instance().install())
                    {
                        RollbackStockTransportObserveHook::instance()
                            .begin_trace();
                    }
                }
                if (NativeBinding::imageBase() == 0)
                    return;
                if (!RollbackLiveBoundaryHook::instance().installed())
                    (void)RollbackLiveBoundaryHook::instance().install();
                m_last_stock_transport_observe_probe =
                    RollbackStockTransportObserveHook::instance().report();
                m_last_live_boundary_probe =
                    RollbackLiveBoundaryHook::instance().report();
                m_last_live_online_capture_probe =
                    EvaluateRollbackLiveOnlineCapture(
                        m_last_stock_transport_observe_probe,
                        m_last_live_boundary_probe);
                m_last_live_activation_candidate =
                    evaluate_live_activation_candidate();
                const uint64_t total =
                    m_last_live_online_capture_probe.total_observed_calls;
                if (tick == 1
                    || total != m_last_live_online_capture_total
                    || (tick % 600) == 0)
                {
                    RollbackDiag::emit_live_online_capture(
                        m_last_live_online_capture_probe, m_config);
                    RollbackDiag::emit_live_activation_candidate(
                        m_last_live_activation_candidate, m_config);
                    m_last_live_online_capture_total = total;
                }
                sync_stock_invite_sidecar();
                m_p2p_harness.service_game_thread(tick);
                sync_stock_invite_sidecar();
                service_sidecar_live_proof(tick);
            }
            else if (m_config.observe_gameflow_requested)
            {
                m_p2p_harness.service_game_thread(tick);
            }
            if (m_config.test_case == RollbackLabCase::OnlineBoundaryTrace
                && !m_live_boundary_probe_ran)
            {
                if (!RollbackLiveBoundaryHook::instance().installed())
                    (void)RollbackLiveBoundaryHook::instance().install();
                m_last_live_boundary_probe =
                    RollbackLiveBoundaryHook::instance().report();
                if (m_last_live_boundary_probe.consumer_count > 0
                    || tick == 1 || (tick % 600) == 0)
                {
                    RollbackDiag::emit_live_boundary(
                        m_last_live_boundary_probe, m_config);
                }
                if (m_last_live_boundary_probe.consumer_count > 0)
                {
                    m_live_boundary_probe_ran = true;
                    RollbackLiveBoundaryHook::instance().end_trace();
                }
            }
            if ((m_config.test_case == RollbackLabCase::CacheInjectionTrace
                 || m_config.test_case == RollbackLabCase::CachePredictionTrace)
                && !m_cache_injection_probe_ran)
            {
                if (!RollbackLiveBoundaryHook::instance().installed())
                    (void)RollbackLiveBoundaryHook::instance().install();
                m_last_cache_injection_probe =
                    RollbackLiveBoundaryHook::instance()
                        .cache_injection_report();
                if (m_last_cache_injection_probe.attempted
                    || tick == 1 || (tick % 600) == 0)
                {
                    RollbackDiag::emit_cache_injection(
                        m_last_cache_injection_probe, m_config);
                }
                if (m_last_cache_injection_probe.attempted)
                {
                    m_cache_injection_probe_ran = true;
                    RollbackLiveBoundaryHook::instance()
                        .end_cache_injection_probe();
                }
            }
            }
            catch (...)
            {
                auto& production = RollbackProductionRuntime::instance();
                if (production.owns_tick_boundary())
                {
                    production.request_fail_closed(
                        "controller-service-exception");
                }
                else
                {
                    shutdown();
                    production.reject_configuration(
                        "controller-service-exception");
                }
            }
        }

    private:
        RollbackSnapshotRoundTripReport run_snapshot_roundtrip_probe()
        {
            RollbackSnapshotRoundTripReport report{};
            report.capture = CaptureRollbackSnapshotBytes(m_manifest, m_snapshot);
            report.before_hash = m_snapshot.hash;
            if (report.capture.ok)
                report.restore = RestoreRollbackSnapshotBytes(m_snapshot);
            if (report.capture.ok && report.restore.ok)
            {
                RollbackSnapshotFrame after {};
                report.recapture =
                    CaptureRollbackSnapshotBytes(m_manifest, after);
                report.after_hash = after.hash;
            }
            report.hash_match =
                report.before_hash != 0
                && report.before_hash == report.after_hash;
            report.ok = report.capture.ok && report.restore.ok
                && report.recapture.ok && report.hash_match;
            return report;
        }

        RollbackHgCpuRoundTripReport run_hgcpu_roundtrip_probe()
        {
            return RollbackHgCpuRoundTrip(
                NativeBinding::imageBase(), m_hgcpu_snapshot);
        }

        RollbackResimWindowReport run_resim_window_probe(
            uint32_t rollback_window,
            bool inject_fault)
        {
            return RunRollbackResimWindowProbe(
                m_manifest, rollback_window, m_config.seed,
                inject_fault);
        }

        RollbackInputLogOwnershipReport run_cache_ownership_probe()
        {
            uint32_t min_master_clock = m_config.rollback_window + 60;
            if (min_master_clock < 120) min_master_clock = 120;
            return RunRollbackInputLogOwnershipProbe(
                m_manifest,
                m_config.rollback_window,
                m_config.seed,
                min_master_clock);
        }

        void refresh_manifest_image_base_if_needed()
        {
            const uintptr_t image_base = NativeBinding::imageBase();
            if (image_base == 0 || image_base == m_manifest.image_base)
                return;
            m_manifest = BuildInitialRollbackManifest(
                image_base, m_config.rollback_window);
            m_snapshot.clear();
            m_hgcpu_snapshot.clear();
            m_snapshot_probe_ran = false;
            m_hgcpu_probe_ran = false;
            m_resim_probe_ran = false;
            m_cache_probe_ran = false;
            m_online_session_probe_ran = false;
            m_live_transport_probe_ran = false;
            m_live_peer_pipeline_probe_ran = false;
            m_end_to_end_probe_ran = false;
            m_live_activation_probe_ran = false;
            m_live_activation_executor_probe_ran = false;
            m_gekko_session_probe_ran = false;
            m_gekko_gameplay_input_probe_ran = false;
            m_gekko_adapter_probe_ran = false;
            m_gekko_udp_probe_ran = false;
            m_live_boundary_probe_ran = false;
            m_cache_injection_probe_ran = false;
            m_last_snapshot_probe = {};
            m_last_hgcpu_probe = {};
            m_last_resim_probe = {};
            m_last_cache_probe = {};
            m_last_online_session_probe = {};
            m_last_live_transport_probe = {};
            m_last_live_peer_pipeline_probe = {};
            m_last_end_to_end_probe = {};
            m_last_live_activation_probe = {};
            m_last_live_activation_executor_probe = {};
            m_last_gekko_session_probe = {};
            m_last_gekko_gameplay_input_probe = {};
            m_last_gekko_adapter_probe = {};
            m_last_gekko_udp_probe = {};
            m_last_live_boundary_probe = {};
            m_last_cache_injection_probe = {};
            m_last_sidecar_probe = {};
            m_sidecar.close();
            m_sidecar.configure(
                m_config.enabled
                    && (m_config.test_case == RollbackLabCase::LiveOnlineCapture
                        || stock_online_production_requested())
                    && m_config.sidecar_requested(),
                m_config.local_peer_id,
                m_config.remote_peer_id,
                m_config.live_activation_session_id,
                m_config.sidecar_local_port,
                m_config.sidecar_remote_port,
                m_config.sidecar_remote_addr,
                m_config.activation_token);
            configure_p2p_harness();
            m_last_sidecar_packets_sent = 0;
            m_last_sidecar_packets_received = 0;
            m_last_sidecar_packets_rejected = 0;
            m_last_sidecar_direct_packets_received = 0;
            m_last_sidecar_direct_packets_rejected = 0;
            m_last_sidecar_ok = false;
            m_last_sidecar_validated_peer = false;
            m_last_sidecar_validated_direct_input = false;
            m_last_sidecar_sendto_error = 0;
            m_last_sidecar_recvfrom_error = 0;
            m_last_sidecar_had_packet_errors = false;
            m_sidecar_bind_emitted = false;
            m_sidecar_handshake_emitted = false;
            m_sidecar_handshake_ok_emitted = false;
            m_live_correction_probe_ran = false;
            m_live_disarm_emitted = false;
            if (m_config.test_case == RollbackLabCase::OnlineBoundaryTrace
                || m_config.test_case == RollbackLabCase::LiveOnlineCapture)
                RollbackLiveBoundaryHook::instance().begin_trace();
            if (m_config.test_case == RollbackLabCase::LiveOnlineCapture)
            {
                RollbackStockTransportObserveHook::instance().begin_trace();
                m_last_stock_transport_observe_total = UINT64_MAX;
                m_last_live_online_capture_total = UINT64_MAX;
                m_last_stock_transport_observe_probe = {};
                m_last_live_online_capture_probe = {};
                m_last_live_activation_candidate = {};
            }
            if (m_config.test_case == RollbackLabCase::CacheInjectionTrace
                || m_config.test_case == RollbackLabCase::CachePredictionTrace)
                begin_configured_cache_probe();
            RollbackDiag::emit_configured(m_config, &m_manifest);
        }

        void reset_probe_state() noexcept
        {
            m_snapshot.clear();
            m_hgcpu_snapshot.clear();
            m_snapshot_probe_ran = false;
            m_hgcpu_probe_ran = false;
            m_resim_probe_ran = false;
            m_cache_probe_ran = false;
            m_online_session_probe_ran = false;
            m_live_transport_probe_ran = false;
            m_live_peer_pipeline_probe_ran = false;
            m_end_to_end_probe_ran = false;
            m_live_activation_probe_ran = false;
            m_live_activation_executor_probe_ran = false;
            m_gekko_session_probe_ran = false;
            m_gekko_gameplay_input_probe_ran = false;
            m_gekko_adapter_probe_ran = false;
            m_live_boundary_probe_ran = false;
            m_cache_injection_probe_ran = false;
            m_last_snapshot_probe = {};
            m_last_hgcpu_probe = {};
            m_last_resim_probe = {};
            m_last_cache_probe = {};
            m_last_online_session_probe = {};
            m_last_live_transport_probe = {};
            m_last_live_peer_pipeline_probe = {};
            m_last_end_to_end_probe = {};
            m_last_live_activation_probe = {};
            m_last_live_activation_executor_probe = {};
            m_last_gekko_session_probe = {};
            m_last_gekko_gameplay_input_probe = {};
            m_last_gekko_adapter_probe = {};
            m_last_stock_transport_observe_total = UINT64_MAX;
            m_last_live_online_capture_total = UINT64_MAX;
            m_last_stock_transport_observe_probe = {};
            m_last_live_online_capture_probe = {};
            m_last_live_activation_candidate = {};
            m_last_live_boundary_probe = {};
            m_last_cache_injection_probe = {};
            m_last_sidecar_probe = {};
            m_sidecar.close();
            m_sidecar.configure(
                m_config.enabled
                    && (m_config.test_case == RollbackLabCase::LiveOnlineCapture
                        || stock_online_production_requested())
                    && m_config.sidecar_requested(),
                m_config.local_peer_id,
                m_config.remote_peer_id,
                m_config.live_activation_session_id,
                m_config.sidecar_local_port,
                m_config.sidecar_remote_port,
                m_config.sidecar_remote_addr,
                m_config.activation_token);
            configure_p2p_harness();
            m_last_sidecar_packets_sent = 0;
            m_last_sidecar_packets_received = 0;
            m_last_sidecar_packets_rejected = 0;
            m_last_sidecar_direct_packets_received = 0;
            m_last_sidecar_direct_packets_rejected = 0;
            m_last_sidecar_ok = false;
            m_last_sidecar_validated_peer = false;
            m_last_sidecar_validated_direct_input = false;
            m_last_sidecar_sendto_error = 0;
            m_last_sidecar_recvfrom_error = 0;
            m_last_sidecar_had_packet_errors = false;
            m_sidecar_bind_emitted = false;
            m_sidecar_handshake_emitted = false;
            m_sidecar_handshake_ok_emitted = false;
            m_live_correction_probe_ran = false;
            m_live_disarm_emitted = false;
            if (m_config.test_case == RollbackLabCase::OnlineBoundaryTrace
                || m_config.test_case == RollbackLabCase::LiveOnlineCapture)
                RollbackLiveBoundaryHook::instance().begin_trace();
            if (m_config.test_case == RollbackLabCase::LiveOnlineCapture)
                RollbackStockTransportObserveHook::instance().begin_trace();
            if (m_config.test_case == RollbackLabCase::CacheInjectionTrace
                || m_config.test_case == RollbackLabCase::CachePredictionTrace)
                begin_configured_cache_probe();
        }

        static bool stock_online_local_lab_requested(
            const RollbackLabConfig& config) noexcept
        {
            return stock_online_production_requested(config)
                && config.online_stage_requested
                && config.stock_join_route
                    == RollbackStockJoinRoute::InjectedStockInvite;
        }

        static bool stock_online_production_requested(
            const RollbackLabConfig& config) noexcept
        {
            return config.test_case == RollbackLabCase::Production
                && config.production.lifecycle_mode
                    == RollbackLifecycleMode::StockOnlinePvp;
        }

        bool stock_online_local_lab_requested() const noexcept
        {
            return stock_online_local_lab_requested(m_config);
        }

        bool stock_online_production_requested() const noexcept
        {
            return stock_online_production_requested(m_config);
        }

        void begin_configured_cache_probe() noexcept
        {
            if (m_config.test_case == RollbackLabCase::CachePredictionTrace)
            {
                RollbackLiveBoundaryHook::instance()
                    .begin_cache_prediction_probe();
                return;
            }
            RollbackLiveBoundaryHook::instance().begin_cache_injection_probe();
        }

        void configure_p2p_harness(const RollbackLabConfig& source)
        {
            const bool stock_online_local_lab =
                stock_online_local_lab_requested(source);
            const bool stock_online_production =
                stock_online_production_requested(source);
            if (source.test_case == RollbackLabCase::Production
                && !stock_online_production)
            {
                m_p2p_harness.shutdown();
                return;
            }
            RollbackP2PHarnessConfig cfg{};
            cfg.enabled =
                source.enabled
                && stock_online_production;
            cfg.online_stage_requested = stock_online_local_lab;
            cfg.stock_session_observe_only =
                stock_online_production && !stock_online_local_lab;
            cfg.live_replay_input_requested =
                source.production.replay_input.enabled;
            cfg.native_lifecycle_trace_requested =
                source.production.replay_deep_trace_diagnostics;
            cfg.native_lifecycle_trace_frame_input_log_only =
                source.native_lifecycle_trace_frame_input_log_only;
            cfg.replay_test_selection_override =
                source.production.replay_test_selection_override;
            cfg.bind_observed_stock_selection =
                source.production.bind_observed_stock_selection;
            cfg.launch_left_character_override =
                source.launch_left_character_override;
            cfg.launch_right_character_override =
                source.launch_right_character_override;
            cfg.launch_left_character_code_override =
                source.launch_left_character_code_override;
            cfg.launch_right_character_code_override =
                source.launch_right_character_code_override;
            cfg.launch_stage_override = source.launch_stage_override;
            cfg.launch_rounds_to_win_override =
                source.launch_rounds_to_win_override;
            cfg.launch_seed = source.production.replay_input.enabled
                ? source.production.replay_input.replay_random_seed
                : source.seed;
            cfg.online_stage_cleanup_only =
                source.online_stage_cleanup_only;
            cfg.online_stage_wait_host_room_ready_marker =
                source.online_stage_wait_host_room_ready_marker;
            cfg.main_user_id_override =
                source.online_stage_main_user_id_override;
            cfg.request_id = source.request_id;
            cfg.client_role = source.client_role;
            cfg.native_session_name =
                source.online_stage_native_session_name;
            cfg.session_name = source.online_stage_session_name;
            cfg.room_name = source.online_stage_room_name;
            cfg.host_room_ready_marker =
                source.online_stage_host_room_ready_marker;
            cfg.online_stage_goal = source.online_stage_goal;
            cfg.stock_join_route = source.stock_join_route;
            cfg.stock_request_generation =
                source.request_generation;
            cfg.stock_build_id =
                source.production.expected_build_id;
            cfg.stock_schema_id =
                source.production.expected_schema_id;
            cfg.stock_selection_hash =
                source.production.expected_selection_hash;
            cfg.local_player_slot =
                source.production.local_player_slot;
            cfg.main_menu_player_match_route =
                source.main_menu_player_match_route;
            cfg.production_steam_session_contract_ready =
                [](void*) noexcept {
                    return RollbackProductionRuntime::instance()
                        .steam_session_contract_ready();
                };
            cfg.bind_production_presentation_actors =
                [](void*) noexcept {
                    auto& production =
                        RollbackProductionRuntime::instance();
                    const auto& status = production.status();
                    return (status.presentation_slot[0].actor_bound
                            && status.presentation_slot[1].actor_bound)
                        || production
                            .bind_presentation_actors_from_round_identity();
                };
            m_p2p_harness.configure(std::move(cfg));
        }

        void configure_p2p_harness()
        {
            configure_p2p_harness(m_config);
        }

        bool arm_cleanup_handoff(
            const RollbackLabConfig& cleanup) noexcept
        {
            try
            {
                configure_p2p_harness(cleanup);
                m_cleanup_handoff_active = true;
                m_production_non_pvp_observations = 0;
                return true;
            }
            catch (...)
            {
                RollbackProductionRuntime::instance().request_fail_closed(
                    "cleanup-harness-allocation-failed");
                return false;
            }
        }

        RollbackLiveActivationReport
        evaluate_live_activation_candidate() const noexcept
        {
            const RollbackStockTransportRoute route =
                RollbackLiveActivationHorseRoute(
                    true,
                    m_config.live_activation_source_peer,
                    m_config.live_activation_destination_peer,
                    m_config.live_activation_session_id);
            const RollbackLiveActivationRequest req {
                m_last_live_online_capture_probe,
                route,
                m_config.live_activation_source_peer,
                m_config.live_activation_destination_peer,
                m_config.live_activation_session_id,
                m_config.live_activation_operator_enable};
            return EvaluateRollbackLiveActivation(req);
        }

        void sync_stock_invite_sidecar() noexcept
        {
            const bool enabled = RollbackStockJoinRouteUsesAuthenticatedOffer(
                m_config.stock_join_route);
            m_p2p_harness.update_stock_invite_peer(
                m_last_sidecar_probe.validated_peer,
                m_last_sidecar_probe.remote_stock_fallback_ready,
                m_last_sidecar_probe.remote_steam_id,
                m_last_sidecar_probe.validated_stock_offer,
                m_last_sidecar_probe.remote_stock_offer);
            const RollbackStockLobbyOffer offer =
                m_p2p_harness.stock_local_offer(
                    m_config.request_generation,
                    m_config.production.expected_build_id,
                    m_config.production.expected_schema_id);
            m_sidecar.configure_stock_lobby_offer(
                // Both consenting peers advertise authenticated invite-lane
                // readiness immediately. Private lobbies are intentionally
                // absent from the public browser, so waiting for browser
                // exhaustion leaves FOnlineSessionSteam's search slot owned
                // and prevents the stock invite handler from queuing its
                // conversion task.
                enabled,
                m_p2p_harness.stock_local_steam_id(),
                m_config.request_generation,
                offer);
        }

        void service_sidecar_live_proof(uint64_t tick) noexcept
        {
            if (!m_config.sidecar_requested())
                return;

            m_last_sidecar_probe = m_sidecar.tick();
            const bool had_packet_errors =
                m_last_sidecar_probe.packets_rejected != 0
                || m_last_sidecar_probe.direct_packets_rejected != 0
                || m_last_sidecar_probe.sendto_error != 0
                || m_last_sidecar_probe.recvfrom_error != 0;
            const bool packet_errors_changed =
                had_packet_errors != m_last_sidecar_had_packet_errors
                || m_last_sidecar_probe.sendto_error
                    != m_last_sidecar_sendto_error
                || m_last_sidecar_probe.recvfrom_error
                    != m_last_sidecar_recvfrom_error;
            const bool sidecar_state_changed =
                m_last_sidecar_probe.ok != m_last_sidecar_ok
                || m_last_sidecar_probe.validated_peer
                    != m_last_sidecar_validated_peer
                || m_last_sidecar_probe.validated_direct_input
                    != m_last_sidecar_validated_direct_input
                || m_last_sidecar_probe.validated_stock_offer
                    != m_last_sidecar_validated_stock_offer
                || m_last_sidecar_probe.remote_stock_fallback_ready
                    != m_last_sidecar_remote_stock_fallback_ready
                || packet_errors_changed;
            const bool heartbeat = (tick % 600) == 0;
            if (!m_sidecar_bind_emitted || sidecar_state_changed
                || heartbeat)
            {
                RollbackDiag::emit_sidecar_bind(
                    m_last_sidecar_probe, m_config);
                m_sidecar_bind_emitted = true;
            }
            if (!m_sidecar_handshake_emitted || sidecar_state_changed
                || (m_last_sidecar_probe.validated_peer
                    && !m_sidecar_handshake_ok_emitted)
                || heartbeat)
            {
                RollbackDiag::emit_sidecar_handshake(
                    m_last_sidecar_probe, m_config);
                m_sidecar_handshake_emitted = true;
                if (m_last_sidecar_probe.validated_peer)
                    m_sidecar_handshake_ok_emitted = true;
            }
            m_last_sidecar_packets_sent = m_last_sidecar_probe.packets_sent;
            m_last_sidecar_packets_received =
                m_last_sidecar_probe.packets_received;
            m_last_sidecar_packets_rejected =
                m_last_sidecar_probe.packets_rejected;
            m_last_sidecar_direct_packets_received =
                m_last_sidecar_probe.direct_packets_received;
            m_last_sidecar_direct_packets_rejected =
                m_last_sidecar_probe.direct_packets_rejected;
            m_last_sidecar_ok = m_last_sidecar_probe.ok;
            m_last_sidecar_validated_peer =
                m_last_sidecar_probe.validated_peer;
            m_last_sidecar_validated_direct_input =
                m_last_sidecar_probe.validated_direct_input;
            m_last_sidecar_validated_stock_offer =
                m_last_sidecar_probe.validated_stock_offer;
            m_last_sidecar_remote_stock_fallback_ready =
                m_last_sidecar_probe.remote_stock_fallback_ready;
            m_last_sidecar_sendto_error = m_last_sidecar_probe.sendto_error;
            m_last_sidecar_recvfrom_error = m_last_sidecar_probe.recvfrom_error;
            m_last_sidecar_had_packet_errors = had_packet_errors;

            if (!m_config.live_activation_operator_enable)
                return;

            if (!m_last_sidecar_probe.ok)
            {
                emit_live_disarm_once(
                    m_last_sidecar_probe.failure
                        ? m_last_sidecar_probe.failure
                        : "sidecar-not-ready");
                return;
            }
            if (!m_last_live_activation_candidate.ok
                || !m_last_live_activation_candidate.activation_ready)
            {
                emit_live_disarm_once(
                    m_last_live_activation_candidate.failure
                        ? m_last_live_activation_candidate.failure
                        : "activation-not-ready");
                return;
            }
        }

        void emit_live_disarm_once(const char* reason) noexcept
        {
            if (m_live_disarm_emitted)
                return;
            m_live_disarm_emitted = true;
            RollbackDiag::emit_live_disarm(
                m_last_sidecar_probe,
                m_last_live_online_capture_probe,
                m_last_live_activation_candidate,
                m_config,
                reason ? reason : "disarmed");
        }

        void refresh_manifest_lifecycle_if_ready()
        {
            const uintptr_t image_base = NativeBinding::imageBase();
            RollbackLifecycleEpoch next {};
            (void)CaptureRollbackLifecycleEpoch(image_base, next);
            next.generation = m_manifest.epoch.generation;

            const bool clock_regressed = RollbackLifecycleClockRegressed(
                m_manifest.epoch, next);
            if (!clock_regressed && m_manifest.epoch.same_as(next))
            {
                m_manifest.epoch.input_log_frame = next.input_log_frame;
                return;
            }

            next.generation = m_manifest.epoch.generation + 1;
            if (next.generation == 0) next.generation = 1;

            m_manifest.epoch = next;
            reset_probe_state();
            RollbackDiag::emit_configured(m_config, &m_manifest);
        }

        RollbackLabConfig m_config {};
        std::optional<RollbackLabConfig> m_pending_config {};
        RollbackSnapshotManifest m_manifest {};
        RollbackSnapshotFrame m_snapshot {};
        RollbackHgCpuSnapshotFrame m_hgcpu_snapshot {};
        RollbackSnapshotRoundTripReport m_last_snapshot_probe {};
        RollbackHgCpuRoundTripReport m_last_hgcpu_probe {};
        RollbackResimWindowReport m_last_resim_probe {};
        RollbackInputLogOwnershipReport m_last_cache_probe {};
        RollbackOnlineSessionSelfTestReport m_last_online_session_probe {};
        RollbackLiveTransportQueueSelfTestReport m_last_live_transport_probe {};
        RollbackLivePeerPipelineSelfTestReport
            m_last_live_peer_pipeline_probe {};
        RollbackEndToEndSelfTestReport m_last_end_to_end_probe {};
        RollbackLiveActivationSelfTestReport m_last_live_activation_probe {};
        RollbackLiveActivationExecutorSelfTestReport
            m_last_live_activation_executor_probe {};
        RollbackGekkoGameplayInputBridgeSelfTestReport
            m_last_gekko_gameplay_input_probe {};
        RollbackGekkoSessionSelfTestReport m_last_gekko_session_probe {};
        RollbackGekkoAdapterSelfTestReport m_last_gekko_adapter_probe {};
        RollbackGekkoUdpAdapterSelfTestReport m_last_gekko_udp_probe {};
        RollbackStockTransportSurfaceSelfTestReport
            m_last_stock_transport_probe {};
        RollbackStockTransportObserveReport
            m_last_stock_transport_observe_probe {};
        RollbackLiveOnlineCaptureReport m_last_live_online_capture_probe {};
        RollbackLiveActivationReport m_last_live_activation_candidate {};
        RollbackLiveBoundaryReport m_last_live_boundary_probe {};
        RollbackCacheInjectionReport m_last_cache_injection_probe {};
        RollbackSidecarReport m_last_sidecar_probe {};
        RollbackSidecarRuntime m_sidecar {};
        RollbackP2PHarness m_p2p_harness {};
        RollbackInputHistory<128> m_history {};
        std::atomic<bool> m_configured {false};
        std::atomic<uint64_t> m_service_ticks {0};
        uint64_t m_last_stock_transport_observe_total {UINT64_MAX};
        uint64_t m_last_live_online_capture_total {UINT64_MAX};
        uint32_t m_last_sidecar_packets_sent {0};
        uint32_t m_last_sidecar_packets_received {0};
        uint64_t m_production_lobby_retry_tick {0};
        uint32_t m_production_non_pvp_observations {0};
        bool m_cleanup_handoff_active {false};
        uintptr_t m_production_fail_closed_battle_manager {0};
        uint64_t m_production_last_trace_tick {0};
        RollbackProductionState m_production_last_trace_state {
            RollbackProductionState::Disabled};
        uint64_t m_production_last_organic_simulation_entries {UINT64_MAX};
        uint32_t m_production_missing_organic_simulation_ticks {0};
        bool m_qualification_active {false};
        uint64_t m_qualification_confirmed_frames_total {0};
        uint64_t m_qualification_completed_confirmed_frames {0};
        uint32_t m_qualification_last_round_generation {UINT32_MAX};
        int32_t m_qualification_last_confirmed_frame {-1};
        uint32_t m_last_sidecar_packets_rejected {0};
        uint32_t m_last_sidecar_direct_packets_received {0};
        uint32_t m_last_sidecar_direct_packets_rejected {0};
        bool m_last_sidecar_ok {false};
        bool m_last_sidecar_validated_peer {false};
        bool m_last_sidecar_validated_direct_input {false};
        bool m_last_sidecar_validated_stock_offer {false};
        bool m_last_sidecar_remote_stock_fallback_ready {false};
        int32_t m_last_sidecar_sendto_error {0};
        int32_t m_last_sidecar_recvfrom_error {0};
        bool m_last_sidecar_had_packet_errors {false};
        bool m_snapshot_probe_ran {false};
        bool m_hgcpu_probe_ran {false};
        bool m_resim_probe_ran {false};
        bool m_cache_probe_ran {false};
        bool m_online_session_probe_ran {false};
        bool m_live_transport_probe_ran {false};
        bool m_live_peer_pipeline_probe_ran {false};
        bool m_end_to_end_probe_ran {false};
        bool m_live_activation_probe_ran {false};
        bool m_live_activation_executor_probe_ran {false};
        bool m_gekko_gameplay_input_probe_ran {false};
        bool m_gekko_session_probe_ran {false};
        bool m_gekko_adapter_probe_ran {false};
        bool m_gekko_udp_probe_ran {false};
        bool m_stock_transport_probe_ran {false};
        bool m_live_boundary_probe_ran {false};
        bool m_cache_injection_probe_ran {false};
        bool m_sidecar_bind_emitted {false};
        bool m_sidecar_handshake_emitted {false};
        bool m_sidecar_handshake_ok_emitted {false};
        bool m_live_correction_probe_ran {false};
        bool m_live_disarm_emitted {false};
    };
}
