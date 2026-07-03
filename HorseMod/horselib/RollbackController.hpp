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
#include "RollbackOnlineSession.hpp"
#include "RollbackSnapshot.hpp"
#include "RollbackStepHarness.hpp"
#include "RollbackStockTransportObserveHook.hpp"
#include "RollbackStockTransportSurface.hpp"
#include "RollbackTransport.hpp"

#include <atomic>
#include <cstring>
#include <cstdint>
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
        std::string output_path;
        std::string request_id;
        std::string source {"default"};
    };

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
        return RollbackLabCase::BaselineOracle;
    }

    class RollbackController
    {
    public:
        void configure(RollbackLabConfig cfg) noexcept
        {
            if (cfg.rollback_window == 0) cfg.rollback_window = 1;
            if (cfg.rollback_window > 60) cfg.rollback_window = 60;
            m_config = std::move(cfg);
            m_manifest = BuildInitialRollbackManifest(
                NativeBinding::imageBase(), m_config.rollback_window);
            m_service_ticks.store(0, std::memory_order_release);
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
            m_configured.store(true, std::memory_order_release);
            RollbackDiag::emit_configured(m_config, &m_manifest);
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

        void shutdown() noexcept
        {
            m_config.enabled = false;
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
            if (!configured())
            {
                RollbackLabConfig cfg{};
                cfg.enabled = enabled;
                cfg.source = "ui";
                configure(std::move(cfg));
                return;
            }
            m_config.enabled = enabled;
            RollbackDiag::emit_configured(m_config, &m_manifest);
            if (m_config.test_case == RollbackLabCase::OnlineBoundaryTrace
                || m_config.test_case == RollbackLabCase::LiveOnlineCapture)
            {
                if (enabled)
                    RollbackLiveBoundaryHook::instance().begin_trace();
                else
                    RollbackLiveBoundaryHook::instance().end_trace();
            }
            if (m_config.test_case == RollbackLabCase::StockTransportObserve
                || m_config.test_case == RollbackLabCase::LiveOnlineCapture)
            {
                if (enabled)
                    RollbackStockTransportObserveHook::instance().begin_trace();
                else
                    RollbackStockTransportObserveHook::instance().end_trace();
            }
            if (m_config.test_case == RollbackLabCase::CacheInjectionTrace
                || m_config.test_case == RollbackLabCase::CachePredictionTrace)
            {
                if (enabled)
                    begin_configured_cache_probe();
                else
                    RollbackLiveBoundaryHook::instance()
                        .end_cache_injection_probe();
            }
        }

        void service_game_thread() noexcept
        {
            if (!enabled()) return;
            refresh_manifest_image_base_if_needed();
            refresh_manifest_lifecycle_if_ready();
            const uint64_t tick =
                m_service_ticks.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (tick == 1 || (tick % 600) == 0)
                RollbackDiag::emit_service_tick(tick, m_config);

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
                    || m_manifest.epoch.presence != 0x03)
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
                    || m_manifest.epoch.presence != 0x03)
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

        void refresh_manifest_lifecycle_if_ready()
        {
            const uintptr_t image_base = NativeBinding::imageBase();
            uintptr_t p1 = 0;
            uintptr_t p2 = 0;
            if (!RollbackReadCharaPointers(image_base, p1, p2))
                return;

            RollbackLifecycleEpoch next = m_manifest.epoch;
            next.chara[0] = p1;
            next.chara[1] = p2;
            next.presence = 0x03;

            if (m_manifest.epoch.same_as(next))
                return;

            m_manifest.epoch = next;
            reset_probe_state();
            RollbackDiag::emit_configured(m_config, &m_manifest);
        }

        RollbackLabConfig m_config {};
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
        RollbackInputHistory<128> m_history {};
        std::atomic<bool> m_configured {false};
        std::atomic<uint64_t> m_service_ticks {0};
        uint64_t m_last_stock_transport_observe_total {UINT64_MAX};
        uint64_t m_last_live_online_capture_total {UINT64_MAX};
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
    };
}
