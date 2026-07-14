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
        bool force_live_prediction_divergence {false};
        bool debug_steam_probe {false};
        bool debug_steam_filter_probe {false};
        bool debug_direct_stage_begin_play {false};
        bool observe_gameflow_requested {false};
        bool observe_gameflow_process_events {false};
        bool online_stage_network_check_compat {false};
        bool online_stage_join_complete_compat {false};
        bool online_stage_transport_ready_compat {false};
        bool online_stage_ready_open_compat {false};
        bool online_stage_peer_route_tag_fix {false};
        bool online_stage_in_room_transition_compat {false};
        bool online_stage_direct_native_join_diagnostic {false};
        bool online_stage_requested {false};
        bool online_stage_no_presence_find {false};
        bool online_stage_cleanup_only {false};
        bool online_stage_find_only {false};
        bool online_stage_wait_host_room_ready_marker {false};
        bool online_stage_diagnostic_reflection {false};
        int32_t online_stage_main_user_id_override {-1};
        bool direct_stage_requested {false};
        bool direct_stage_observe_only {false};
        bool direct_connect_requested {false};
        bool direct_replay_input_requested {false};
        bool direct_correction_requested {false};
        bool live_replay_input_requested {false};
        int32_t launch_left_character_override {-1};
        int32_t launch_right_character_override {-1};
        int32_t launch_stage_override {-1};
        std::string online_stage_native_session_name;
        std::string online_stage_session_name;
        std::string online_stage_room_name;
        std::string online_stage_host_room_ready_marker;
        std::string online_stage_goal {"player-match-battle"};
        std::string replay_input_file;
        std::string main_menu_player_match_route;
        uint64_t online_stage_target_owner_id {0};
        uint64_t online_stage_invite_target_id {0};
        uint64_t online_stage_join_lobby_id {0};
        RollbackStockJoinRoute stock_join_route {
            RollbackStockJoinRoute::Browser};
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
        RollbackProductionConfig production {};

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
        if (cfg.test_case == RollbackLabCase::ReplayForkLab)
        {
            cfg.production.session_domain =
                RollbackSessionDomain::ReplayForkLab;
            return;
        }
        cfg.production.launch_descriptor.seed = cfg.seed;
        if (cfg.launch_left_character_override >= 0)
            cfg.production.launch_descriptor.left_character =
                cfg.launch_left_character_override;
        if (cfg.launch_right_character_override >= 0)
            cfg.production.launch_descriptor.right_character =
                cfg.launch_right_character_override;
        if (cfg.launch_stage_override >= 0)
            cfg.production.launch_descriptor.stage =
                cfg.launch_stage_override;
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
        if (s == "replay-fork-lab" || s == "replay-fork"
            || s == "direct-connect")
            return RollbackLabCase::ReplayForkLab;
        if (s == "production" || s == "udp-production"
            || s == "gekko-production")
            return RollbackLabCase::Production;
        return RollbackLabCase::BaselineOracle;
    }

    static inline RollbackLifecycleMode rollback_lifecycle_mode_from_string(
        const std::string& value) noexcept
    {
        if (value == "mirrored-versus" || value == "mirrored_versus"
            || value == "local-versus")
        {
            return RollbackLifecycleMode::MirroredVersus;
        }
        return RollbackLifecycleMode::StockOnlinePvp;
    }

    class RollbackController
    {
    public:
        void configure(RollbackLabConfig cfg) noexcept
        {
            NormalizeRollbackProductionConfig(cfg);
            auto& production = RollbackProductionRuntime::instance();
            if (production.owns_tick_boundary())
            {
                const RollbackProductionState state =
                    production.status().state;
                const bool duplicate_active_request =
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
                    // Phase workers intentionally rewrite the immutable
                    // request to obtain a fresh acknowledgement while the
                    // authenticated session remains active. Acknowledge the
                    // replay without disturbing tick ownership or transport.
                    RollbackDiag::emit_configured(m_config, &m_manifest);
                    return;
                }
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
                production.request_fail_closed("operator-reconfigure");
                return;
            }
            try
            {
            m_config = std::move(cfg);
            m_manifest = BuildInitialRollbackManifest(
                NativeBinding::imageBase(), m_config.rollback_window);
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
            m_service_ticks.store(0, std::memory_order_release);
            m_production_lobby_retry_tick = 0;
            m_production_non_pvp_observations = 0;
            m_production_fail_closed_battle_manager = 0;
            m_production_last_trace_tick = 0;
            m_production_last_trace_state =
                RollbackProductionState::Disabled;
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
            m_direct_connect_emitted = false;
            m_direct_connect_ok_emitted = false;
            m_sidecar_direct_input_configured = false;
            m_live_correction_probe_ran = false;
            m_live_disarm_emitted = false;
            m_sidecar.configure(
                m_config.enabled
                    && m_config.test_case == RollbackLabCase::LiveOnlineCapture
                    && m_config.sidecar_requested(),
                m_config.local_peer_id,
                m_config.remote_peer_id,
                m_config.live_activation_session_id,
                m_config.sidecar_local_port,
                m_config.sidecar_remote_port,
                m_config.sidecar_remote_addr,
                m_config.activation_token);
            configure_p2p_harness();
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
            RollbackProductionRuntime::instance().shutdown();
            RollbackReplayForkRuntime::instance().shutdown(true);
            m_pending_config.reset();
            m_config.enabled = false;
            m_production_lobby_retry_tick = 0;
            m_production_non_pvp_observations = 0;
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
            m_direct_connect_emitted = false;
            m_direct_connect_ok_emitted = false;
            m_sidecar_direct_input_configured = false;
            m_live_correction_probe_ran = false;
            m_live_disarm_emitted = false;
            RollbackLiveBoundaryHook::instance().end_trace();
            RollbackLiveBoundaryHook::instance().end_cache_injection_probe();
            RollbackStockTransportObserveHook::instance().end_trace();
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
            const uint64_t tick =
                m_service_ticks.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (tick == 1 || (tick % 600) == 0)
                RollbackDiag::emit_service_tick(tick, m_config);
            if (m_config.test_case == RollbackLabCase::Production)
            {
                auto& production = RollbackProductionRuntime::instance();
                production.service_game_thread(m_manifest);
                if (m_config.production.lifecycle_mode
                    == RollbackLifecycleMode::MirroredVersus)
                {
                    m_p2p_harness.service_game_thread(tick);
                }
                const RollbackProductionStatus& status = production.status();
                if (tick == 1 || status.state !=
                        m_production_last_trace_state ||
                    tick - m_production_last_trace_tick >= 30)
                {
                    const RollbackManifestValidationReport manifest_report =
                        ValidateRollbackSnapshotManifest(m_manifest, true);
                    ReplayTraceFields fields;
                    fields.string("request_id", m_config.request_id)
                        .string("client_role", m_config.client_role)
                        .string("lifecycle_mode", RollbackLifecycleModeName(
                            m_config.production.lifecycle_mode))
                        .uinteger("state",
                            static_cast<uint8_t>(status.state))
                        .string("failure", status.failure
                            ? status.failure : "unknown")
                        .boolean("executable_match",
                            status.executable_match)
                        .hex("executable_id", status.executable_id)
                        .hex("expected_build_id",
                            m_config.production.expected_build_id)
                        .boolean("schema_match", status.schema_match)
                        .hex("schema_id", status.schema_id)
                        .hex("expected_schema_id",
                            m_config.production.expected_schema_id)
                        .boolean("manifest_ready", status.manifest_ready)
                        .uinteger("pending_gameplay_entries",
                            manifest_report.pending_gameplay_entries)
                        .boolean("lifecycle_ready", status.lifecycle_ready)
                        .boolean("peer_ready", status.peer_ready)
                        .uinteger("native_input_source_slot",
                            status.native_input_source_slot)
                        .uinteger("gekko_slot",
                            status.local_player_slot)
                        .boolean("tick_hook_installed",
                            status.tick_hook_installed)
                        .boolean("presentation_hooks_installed",
                            status.presentation_hooks_installed)
                        .hex("desired_descriptor_hash",
                            status.desired_launch_descriptor_hash)
                        .hex("observed_descriptor_hash",
                            status.observed_launch_descriptor_hash)
                        .hex("peer_descriptor_hash",
                            status.peer_launch_descriptor_hash)
                        .boolean("setup_barrier_local",
                            status.launch_setup_local)
                        .boolean("setup_barrier_peer",
                            status.launch_setup_peer)
                        .boolean("baseline_barrier_local",
                            status.launch_baseline_local)
                        .boolean("baseline_barrier_peer",
                            status.launch_baseline_peer)
                        .integer("baseline_frame",
                            status.launch_baseline_frame)
                        .hex("baseline_epoch",
                            status.launch_baseline_epoch)
                        .hex("peer_baseline_epoch",
                            status.peer_launch_baseline_epoch)
                        .hex("baseline_hash",
                            status.launch_baseline_hash)
                        .hex("peer_baseline_hash",
                            status.peer_launch_baseline_hash)
                        .uinteger("canonical_stage_identity",
                            status.launch_stage_identity)
                        .uinteger("peer_canonical_stage_identity",
                            status.peer_launch_stage_identity)
                        .hex("local_input_hash",
                            status.local_input_hash)
                        .hex("remote_input_hash",
                            status.remote_input_hash)
                        .uinteger("local_input_count",
                            status.local_input_count)
                        .uinteger("remote_input_count",
                            status.remote_input_count)
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
                        .boolean("presentation_exactly_once",
                            status.presentation_exactly_once)
                        .uinteger("presentation_queued",
                            status.presentation_queued)
                        .uinteger("presentation_duplicates_suppressed",
                            status.presentation_duplicates_suppressed)
                        .uinteger("presentation_discarded",
                            status.presentation_discarded)
                        .uinteger("presentation_committed",
                            status.presentation_committed)
                        .string("network_profile",
                            RollbackNetworkProfileName(
                                static_cast<RollbackNetworkProfileKind>(
                                    status.network_profile)))
                        .hex("fault_seed", status.fault_seed)
                        .uinteger("fault_submitted",
                            status.fault_packets_submitted)
                        .uinteger("fault_queued", status.fault_packets_queued)
                        .uinteger("fault_delivered",
                            status.fault_packets_delivered)
                        .uinteger("fault_dropped", status.fault_packets_dropped)
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
                        .uinteger("fault_queue_overflows",
                            status.fault_queue_overflows)
                        .uinteger("saves", status.saves)
                        .uinteger("loads", status.loads)
                        .uinteger("advances", status.advances)
                        .uinteger("rollback_advances",
                            status.rollback_advances)
                        .uinteger("pair_accepts", status.pair_accepts)
                        .integer("corrected_frame",
                            status.corrected_frame.valid
                                ? static_cast<int64_t>(
                                    status.corrected_frame.value) : -1)
                        .integer("confirmed_frame",
                            status.confirmed_frame.valid
                                ? static_cast<int64_t>(
                                    status.confirmed_frame.value) : -1);
                    ReplayDebugTrace::instance().event(
                        "rollback_production_status", fields);
                    m_production_last_trace_tick = tick;
                    m_production_last_trace_state = status.state;
                }
                const RollbackLifecycleEpoch& live_epoch = m_manifest.epoch;
                const bool stable_non_pvp_observation =
                    live_epoch.presence <= 13
                    && (m_config.production.lifecycle_mode
                            == RollbackLifecycleMode::MirroredVersus
                        ? live_epoch.presence != 5
                        : (!live_epoch.pvp_active
                           && live_epoch.presence != 7
                           && live_epoch.presence != 8));
                if (stable_non_pvp_observation)
                {
                    if (m_production_non_pvp_observations < 3)
                        ++m_production_non_pvp_observations;
                }
                else
                {
                    m_production_non_pvp_observations = 0;
                }
                if (tick == 1 || (tick % 600) == 0)
                {
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[RollbackProduction] state={} failure={} "
                        "build={:#x} expected_build={:#x} schema={:#x} "
                        "expected_schema={:#x} epoch={} saves={} loads={} "
                        "advances={} rollback_advances={} pair_accepts={} "
                        "mode={} native_input={} gekko_slot={} "
                        "launch_ready={} launch_hash={:#x} "
                        "peer_launch_hash={:#x}\n"),
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
                        status.launch_barrier_ready ? 1 : 0,
                        status.observed_launch_descriptor_hash,
                        status.peer_launch_descriptor_hash);
                }
                if (status.lobby_return_requested
                    && m_production_non_pvp_observations < 3
                    && (m_production_lobby_retry_tick == 0
                        || tick - m_production_lobby_retry_tick >= 60))
                {
                    m_production_lobby_retry_tick = tick;
                    std::string reason;
                    const bool dispatched =
                        m_p2p_harness.request_fail_closed_disconnect(
                            m_manifest.epoch.valid
                                && m_manifest.epoch.battle_manager != 0
                                ? m_manifest.epoch.battle_manager
                                : m_production_fail_closed_battle_manager,
                            reason);
                    production.record_lobby_return_dispatch(dispatched);
                    RC::Output::send<RC::LogLevel::Error>(STR(
                        "[RollbackProduction] fail-closed disconnect "
                        "dispatched={} reason={}\n"),
                        dispatched ? 1 : 0,
                        RC::to_generic_string(reason));
                }
                if (status.lobby_return_requested
                    && m_production_non_pvp_observations >= 3)
                {
                    // Three independently captured non-PVP observations are
                    // the teardown proof. A console command merely being
                    // accepted is diagnostic and never releases the frozen
                    // boundary by itself.
                    production.shutdown();
                    if (m_pending_config)
                    {
                        RollbackLabConfig pending =
                            std::move(*m_pending_config);
                        m_pending_config.reset();
                        configure(std::move(pending));
                        return;
                    }
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
                configure_sidecar_direct_input_if_ready();
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
                    && m_config.test_case == RollbackLabCase::LiveOnlineCapture
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
            m_direct_connect_emitted = false;
            m_direct_connect_ok_emitted = false;
            m_sidecar_direct_input_configured = false;
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
                    && m_config.test_case == RollbackLabCase::LiveOnlineCapture
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
            m_direct_connect_emitted = false;
            m_direct_connect_ok_emitted = false;
            m_sidecar_direct_input_configured = false;
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

        void configure_p2p_harness()
        {
            const bool mirrored_versus =
                m_config.test_case == RollbackLabCase::Production
                && m_config.production.lifecycle_mode
                    == RollbackLifecycleMode::MirroredVersus;
            if (m_config.test_case == RollbackLabCase::Production
                && !mirrored_versus)
            {
                // The production UDP/Gekko runtime does not own or configure
                // the legacy diagnostic P2P harness.  Keep only its
                // allocation-free disconnect helper available.
                m_p2p_harness.shutdown();
                return;
            }
            RollbackP2PHarnessConfig cfg{};
            const bool needs_live_p2p =
                m_config.test_case == RollbackLabCase::LiveOnlineCapture
                && (m_config.online_stage_requested
                    || m_config.live_replay_input_requested
                    || m_config.direct_stage_requested
                    || m_config.direct_connect_requested
                    || m_config.direct_replay_input_requested
                    || m_config.direct_correction_requested);
            cfg.enabled =
                m_config.enabled
                && (needs_live_p2p
                    || m_config.observe_gameflow_requested
                    || mirrored_versus);
            cfg.online_stage_requested =
                mirrored_versus || m_config.online_stage_requested;
            cfg.observe_gameflow_requested =
                m_config.observe_gameflow_requested;
            cfg.observe_gameflow_process_events =
                m_config.observe_gameflow_process_events;
            cfg.direct_stage_requested = m_config.direct_stage_requested;
            cfg.direct_stage_observe_only =
                m_config.direct_stage_observe_only;
            cfg.direct_connect_requested = m_config.direct_connect_requested;
            cfg.direct_replay_input_requested =
                m_config.direct_replay_input_requested;
            cfg.direct_correction_requested =
                m_config.direct_correction_requested;
            cfg.mirrored_versus_requested = mirrored_versus;
            cfg.mirrored_launch_descriptor =
                m_config.production.launch_descriptor;
            cfg.launch_left_character_override =
                m_config.launch_left_character_override;
            cfg.launch_right_character_override =
                m_config.launch_right_character_override;
            cfg.launch_stage_override = m_config.launch_stage_override;
            cfg.launch_seed = m_config.seed;
            cfg.live_replay_input_requested =
                m_config.live_replay_input_requested;
            cfg.force_live_prediction_divergence =
                m_config.force_live_prediction_divergence;
            cfg.debug_steam_probe = m_config.debug_steam_probe;
            cfg.debug_steam_filter_probe =
                m_config.debug_steam_filter_probe;
            cfg.debug_direct_stage_begin_play =
                m_config.debug_direct_stage_begin_play;
            cfg.online_stage_network_check_compat =
                m_config.online_stage_network_check_compat;
            // These two experiments manually invoked native completion or
            // forced transport readiness and produced crash-prone states.
            // Keep the legacy input/report fields, but never forward them to
            // the live harness.
            cfg.online_stage_join_complete_compat = false;
            cfg.online_stage_transport_ready_compat = false;
            cfg.online_stage_ready_open_compat =
                m_config.online_stage_ready_open_compat;
            cfg.online_stage_peer_route_tag_fix =
                m_config.online_stage_peer_route_tag_fix;
            cfg.online_stage_in_room_transition_compat =
                m_config.online_stage_in_room_transition_compat;
            cfg.online_stage_direct_native_join_diagnostic =
                m_config.online_stage_direct_native_join_diagnostic;
            cfg.online_stage_no_presence_find =
                m_config.online_stage_no_presence_find;
            cfg.online_stage_cleanup_only =
                m_config.online_stage_cleanup_only;
            cfg.online_stage_find_only = m_config.online_stage_find_only;
            cfg.online_stage_wait_host_room_ready_marker =
                m_config.online_stage_wait_host_room_ready_marker;
            cfg.online_stage_diagnostic_reflection =
                m_config.online_stage_diagnostic_reflection;
            cfg.main_user_id_override =
                m_config.online_stage_main_user_id_override;
            cfg.request_id = m_config.request_id;
            cfg.client_role = m_config.client_role;
            cfg.native_session_name =
                m_config.online_stage_native_session_name;
            cfg.session_name = m_config.online_stage_session_name;
            cfg.room_name = m_config.online_stage_room_name;
            cfg.host_room_ready_marker =
                m_config.online_stage_host_room_ready_marker;
            cfg.online_stage_goal = mirrored_versus
                ? "main-menu" : m_config.online_stage_goal;
            cfg.target_owner_id = m_config.online_stage_target_owner_id;
            cfg.invite_target_id = m_config.online_stage_invite_target_id;
            cfg.join_lobby_id = m_config.online_stage_join_lobby_id;
            cfg.stock_join_route = m_config.stock_join_route;
            cfg.stock_request_generation =
                m_config.request_generation;
            cfg.stock_build_id =
                m_config.production.expected_build_id;
            cfg.stock_schema_id =
                m_config.production.expected_schema_id;
            cfg.replay_input_file = m_config.replay_input_file;
            cfg.main_menu_player_match_route =
                m_config.main_menu_player_match_route;
            cfg.local_replay_player = m_config.local_replay_player;
            cfg.remote_replay_player = m_config.remote_replay_player;
            cfg.divergence_frame = m_config.replay_divergence_frame;
            cfg.divergence_window = m_config.replay_divergence_window;
            m_p2p_harness.configure(std::move(cfg));
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

        bool direct_mode_requested() const noexcept
        {
            return m_config.direct_stage_requested
                || m_config.direct_connect_requested
                || m_config.direct_replay_input_requested
                || m_config.direct_correction_requested;
        }

        bool direct_input_exchange_requested() const noexcept
        {
            return m_config.direct_connect_requested
                || m_config.direct_replay_input_requested
                || m_config.direct_correction_requested;
        }

        void configure_sidecar_direct_input_if_ready() noexcept
        {
            if (!direct_input_exchange_requested())
                return;

            const bool ready = m_p2p_harness.replay_input_metadata_ready();
            m_sidecar.configure_direct_input(
                ready,
                m_p2p_harness.local_replay_player(),
                m_p2p_harness.remote_replay_player(),
                0,
                m_p2p_harness.replay_input_frame_count(),
                m_p2p_harness.local_replay_input_hash(),
                m_p2p_harness.expected_remote_replay_input_hash());
            m_sidecar_direct_input_configured =
                m_sidecar_direct_input_configured || ready;
        }

        void sync_stock_invite_sidecar() noexcept
        {
            const bool enabled = m_config.stock_join_route ==
                RollbackStockJoinRoute::InviteFallback;
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
                enabled && (m_config.client_role == "host"
                    || m_p2p_harness.stock_fallback_active()),
                m_p2p_harness.stock_local_steam_id(),
                m_config.request_generation,
                offer);
        }

        void service_direct_correction_live_proof(uint64_t tick) noexcept
        {
            if (!m_config.direct_correction_requested)
                return;

            if (!m_last_sidecar_probe.ok
                || !m_last_sidecar_probe.validated_direct_input)
            {
                emit_live_disarm_once(
                    m_last_sidecar_probe.failure
                        ? m_last_sidecar_probe.failure
                        : "direct-peer-not-ready");
                return;
            }
            if (!m_p2p_harness.direct_stage_ready())
            {
                const char* reason = m_p2p_harness.direct_stage_failure();
                emit_live_disarm_once(
                    reason ? reason : "direct-stage-not-ready");
                return;
            }
            if (!m_p2p_harness.replay_input_metadata_ready())
            {
                emit_live_disarm_once("direct-replay-input-not-ready");
                return;
            }
            if (!m_config.force_live_prediction_divergence
                || m_live_correction_probe_ran)
                return;

            m_last_resim_probe = run_resim_window_probe(
                m_config.rollback_window,
                true);
            const bool waiting_for_context =
                !m_last_resim_probe.context_ready
                && m_last_resim_probe.failure
                && std::strcmp(
                    m_last_resim_probe.failure,
                    "battle-context-not-ready") == 0;
            if (waiting_for_context)
            {
                if (tick == 1 || (tick % 600) == 0)
                    RollbackDiag::emit_direct_correction(
                        m_last_sidecar_probe,
                        m_last_resim_probe,
                        m_config);
                return;
            }

            m_live_correction_probe_ran = true;
            RollbackDiag::emit_resim_window(m_last_resim_probe, m_config);
            RollbackDiag::emit_direct_correction(
                m_last_sidecar_probe,
                m_last_resim_probe,
                m_config);
            if (!m_last_resim_probe.ok)
            {
                emit_live_disarm_once(
                    m_last_resim_probe.failure
                        ? m_last_resim_probe.failure
                        : "direct-correction-failed");
            }
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
            if (direct_mode_requested()
                && (!m_direct_connect_emitted
                    || sidecar_state_changed
                    || (m_last_sidecar_probe.validated_direct_input
                        && !m_direct_connect_ok_emitted)
                    || heartbeat))
            {
                RollbackDiag::emit_direct_connect(
                    m_last_sidecar_probe,
                    m_config);
                m_direct_connect_emitted = true;
                if (m_last_sidecar_probe.validated_direct_input)
                    m_direct_connect_ok_emitted = true;
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

            if (m_config.direct_correction_requested)
            {
                service_direct_correction_live_proof(tick);
                return;
            }

            if (!m_config.live_activation_operator_enable
                && !m_config.force_live_prediction_divergence)
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
            if (!m_config.force_live_prediction_divergence
                || m_live_correction_probe_ran)
                return;

            m_last_resim_probe = run_resim_window_probe(
                m_config.rollback_window,
                true);
            const bool waiting_for_context =
                !m_last_resim_probe.context_ready
                && m_last_resim_probe.failure
                && std::strcmp(
                    m_last_resim_probe.failure,
                    "battle-context-not-ready") == 0;
            if (waiting_for_context)
            {
                if (tick == 1 || (tick % 600) == 0)
                    RollbackDiag::emit_live_disarm(
                        m_last_sidecar_probe,
                        m_last_live_online_capture_probe,
                        m_last_live_activation_candidate,
                        m_config,
                        m_last_resim_probe.failure);
                return;
            }

            m_live_correction_probe_ran = true;
            RollbackDiag::emit_resim_window(m_last_resim_probe, m_config);
            RollbackDiag::emit_live_cache_write(
                m_last_sidecar_probe,
                m_last_live_online_capture_probe,
                m_last_live_activation_candidate,
                m_last_resim_probe,
                m_config);
            RollbackDiag::emit_live_correction(
                m_last_sidecar_probe,
                m_last_live_online_capture_probe,
                m_last_live_activation_candidate,
                m_last_resim_probe,
                m_config);
            RollbackDiag::emit_live_convergence(
                m_last_sidecar_probe,
                m_last_live_online_capture_probe,
                m_last_live_activation_candidate,
                m_last_resim_probe,
                m_config);
            if (!m_last_resim_probe.ok)
            {
                emit_live_disarm_once(
                    m_last_resim_probe.failure
                        ? m_last_resim_probe.failure
                        : "live-correction-failed");
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
        uintptr_t m_production_fail_closed_battle_manager {0};
        uint64_t m_production_last_trace_tick {0};
        RollbackProductionState m_production_last_trace_state {
            RollbackProductionState::Disabled};
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
        bool m_direct_connect_emitted {false};
        bool m_direct_connect_ok_emitted {false};
        bool m_sidecar_direct_input_configured {false};
        bool m_live_correction_probe_ran {false};
        bool m_live_disarm_emitted {false};
    };
}
