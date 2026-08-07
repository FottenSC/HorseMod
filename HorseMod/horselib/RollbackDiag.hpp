// ============================================================================
// Horse::RollbackDiag
//
// JSONL/log helpers for rollback lab observability. All calls are best-effort
// and must never affect gameplay.
// ============================================================================

#pragma once

#include "ReplayDebugTrace.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <cstdint>

namespace Horse
{
    struct RollbackLabConfig;
    struct RollbackSnapshotManifest;
    struct RollbackSnapshotRoundTripReport;
    struct RollbackHgCpuRoundTripReport;
    struct RollbackResimWindowReport;
    struct RollbackInputLogOwnershipReport;
    struct RollbackGekkoAdapterSelfTestReport;
    struct RollbackGekkoGameplayInputBridgeSelfTestReport;
    struct RollbackGekkoSessionSelfTestReport;
    struct RollbackGekkoUdpAdapterSelfTestReport;
    struct RollbackOnlineSessionSelfTestReport;
    struct RollbackLiveTransportQueueSelfTestReport;
    struct RollbackLivePeerPipelineSelfTestReport;
    struct RollbackEndToEndSelfTestReport;
    struct RollbackLiveActivationReport;
    struct RollbackLiveActivationSelfTestReport;
    struct RollbackLiveActivationExecutorSelfTestReport;
    struct RollbackStockTransportSurfaceSelfTestReport;
    struct RollbackStockTransportObserveReport;
    struct RollbackLiveOnlineCaptureReport;
    struct RollbackLiveBoundaryReport;
    struct RollbackCacheInjectionReport;
    struct RollbackSidecarReport;

    class RollbackDiag
    {
    public:
        static void emit_configured(
            const RollbackLabConfig& cfg,
            const RollbackSnapshotManifest* manifest = nullptr) noexcept;
        static void emit_service_tick(uint64_t tick, const RollbackLabConfig& cfg) noexcept;
        static void emit_two_client_role_manifest(
            const RollbackLabConfig& cfg,
            const RollbackSnapshotManifest* manifest = nullptr) noexcept;
        static void emit_snapshot_roundtrip(
            const RollbackSnapshotRoundTripReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_hgcpu_roundtrip(
            const RollbackHgCpuRoundTripReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_resim_window(
            const RollbackResimWindowReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_cache_ownership(
            const RollbackInputLogOwnershipReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_online_session_selftest(
            const RollbackOnlineSessionSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_transport_selftest(
            const RollbackLiveTransportQueueSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_peer_pipeline_selftest(
            const RollbackLivePeerPipelineSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_end_to_end_selftest(
            const RollbackEndToEndSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_activation_selftest(
            const RollbackLiveActivationSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_activation_candidate(
            const RollbackLiveActivationReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_activation_executor_selftest(
            const RollbackLiveActivationExecutorSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_gekko_session_selftest(
            const RollbackGekkoSessionSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_gekko_adapter_selftest(
            const RollbackGekkoAdapterSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_gekko_udp_selftest(
            const RollbackGekkoUdpAdapterSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_gekko_gameplay_input_selftest(
            const RollbackGekkoGameplayInputBridgeSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_stock_transport_selftest(
            const RollbackStockTransportSurfaceSelfTestReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_stock_transport_observe(
            const RollbackStockTransportObserveReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_online_capture(
            const RollbackLiveOnlineCaptureReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_boundary(
            const RollbackLiveBoundaryReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_cache_injection(
            const RollbackCacheInjectionReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_sidecar_bind(
            const RollbackSidecarReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_sidecar_handshake(
            const RollbackSidecarReport& report,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_cache_write(
            const RollbackSidecarReport& sidecar,
            const RollbackLiveOnlineCaptureReport& capture,
            const RollbackLiveActivationReport& activation,
            const RollbackResimWindowReport& resim,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_correction(
            const RollbackSidecarReport& sidecar,
            const RollbackLiveOnlineCaptureReport& capture,
            const RollbackLiveActivationReport& activation,
            const RollbackResimWindowReport& resim,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_convergence(
            const RollbackSidecarReport& sidecar,
            const RollbackLiveOnlineCaptureReport& capture,
            const RollbackLiveActivationReport& activation,
            const RollbackResimWindowReport& resim,
            const RollbackLabConfig& cfg) noexcept;
        static void emit_live_disarm(
            const RollbackSidecarReport& sidecar,
            const RollbackLiveOnlineCaptureReport& capture,
            const RollbackLiveActivationReport& activation,
            const RollbackLabConfig& cfg,
            const char* reason) noexcept;
    };
}
