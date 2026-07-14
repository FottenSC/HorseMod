// ============================================================================
// Horse::RollbackProductionRuntime
//
// Horse-owned UDP/Gekko rollback runtime. Socket I/O is isolated to
// RollbackUdpNetworkWorker; Gekko, snapshots, lifecycle validation and native
// simulation execute only from SC6's LuxBattle_PerFrameTick game-thread hook.
// ============================================================================

#pragma once

#ifndef HORSE_ENABLE_GEKKONET
#define HORSE_ENABLE_GEKKONET 0
#endif

#include "RollbackGekkoGameplayInputBridge.hpp"
#include "RollbackGekkoRuntimeCore.hpp"
#include "RollbackGekkoSessionStart.hpp"
#include "RollbackHistoricalCameraArgs.hpp"
#include "RollbackProductionActiveGuard.hpp"
#include "RollbackSideEffectLedger.hpp"
#include "RollbackSnapshotStore.hpp"
#include "RollbackStepHarness.hpp"
#include "RollbackSummaryConsensus.hpp"
#include "RollbackUdpRuntime.hpp"

#include <Windows.h>
#include <polyhook2/Detour/x64Detour.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>

#if HORSE_ENABLE_GEKKONET
#include <gekkonet.h>
#endif

namespace Horse
{
    static constexpr uintptr_t kRollbackProductionRvaPerFrameTick = 0x2DBC60;
    static constexpr uintptr_t kRollbackProductionRvaAudioCue = 0x3110B0;
    static constexpr uintptr_t kRollbackProductionRvaAudioSlot = 0x311190;
    static constexpr uintptr_t kRollbackProductionRvaWindParticles = 0x334960;
    static constexpr uintptr_t kRollbackProductionRvaRandU32 = 0x34F130;
    static constexpr uintptr_t kRollbackProductionRvaRandNormalize = 0x3E89F94;
    static constexpr uintptr_t kRollbackProductionRvaVfxDispatcher = 0x470D188;
    static constexpr uintptr_t kRollbackProductionRvaVmFreeze = 0x48462D0;
    static constexpr uintptr_t kRollbackProductionRvaSetSceneVisibility =
        0x1DAD440;

    enum class RollbackProductionState : uint8_t
    {
        Disabled,
        WaitingForContext,
        WaitingForPeer,
        WaitingForLaunchBarrier,
        WaitingForGekko,
        Active,
        Fatal,
        Stopping,
    };

    struct RollbackProductionStatus
    {
        RollbackProductionState state {RollbackProductionState::Disabled};
        bool executable_match {false};
        bool schema_match {false};
        bool manifest_ready {false};
        bool lifecycle_ready {false};
        bool peer_ready {false};
        bool launch_setup_local {false};
        bool launch_setup_peer {false};
        bool launch_baseline_local {false};
        bool launch_baseline_peer {false};
        bool launch_barrier_ready {false};
        bool gekko_ready {false};
        bool tick_hook_installed {false};
        bool presentation_hooks_installed {false};
        bool lobby_return_requested {false};
        bool lobby_return_dispatched {false};
        bool lobby_return_succeeded {false};
        uint64_t executable_id {0};
        uint64_t schema_id {0};
        uint64_t epoch {0};
        uint64_t advances {0};
        uint64_t rollback_advances {0};
        uint64_t saves {0};
        uint64_t loads {0};
        uint64_t pair_accepts {0};
        uint64_t desired_launch_descriptor_hash {0};
        uint64_t observed_launch_descriptor_hash {0};
        uint64_t peer_launch_descriptor_hash {0};
        uint64_t launch_baseline_hash {0};
        uint64_t peer_launch_baseline_hash {0};
        uint64_t launch_baseline_epoch {0};
        uint64_t peer_launch_baseline_epoch {0};
        uint32_t launch_stage_identity {0};
        uint32_t peer_launch_stage_identity {0};
        uint64_t local_input_hash {0};
        uint64_t remote_input_hash {0};
        uint64_t local_input_count {0};
        uint64_t remote_input_count {0};
        uint64_t confirmed_canonical_hash {0};
        uint64_t last_restore_expected_hash {0};
        uint64_t last_restore_observed_hash {0};
        uint64_t presentation_queued {0};
        uint64_t presentation_duplicates_suppressed {0};
        uint64_t presentation_discarded {0};
        uint64_t presentation_committed {0};
        uint64_t network_packets_sent {0};
        uint64_t network_packets_received {0};
        uint64_t network_packets_rejected {0};
        uint64_t fault_packets_submitted {0};
        uint64_t fault_packets_queued {0};
        uint64_t fault_packets_delivered {0};
        uint64_t fault_packets_dropped {0};
        uint64_t fault_packets_duplicated {0};
        uint64_t fault_packets_reordered {0};
        uint64_t fault_packets_corrupted {0};
        uint64_t fault_packets_spiked {0};
        uint64_t fault_packets_burst_dropped {0};
        uint64_t fault_queue_overflows {0};
        uint32_t fault_seed {0};
        int32_t launch_baseline_frame {-1};
        bool baseline_restore_verified {false};
        bool prediction_restore_verified {false};
        bool final_restore_verified {false};
        bool presentation_exactly_once {true};
        uint8_t lifecycle_mode {0};
        uint8_t local_player_slot {0};
        uint8_t native_input_source_slot {0};
        uint8_t network_profile {0};
        RollbackFrameStamp corrected_frame {};
        RollbackFrameStamp confirmed_frame {};
        const char* failure {"disabled"};
    };

    static inline uint64_t ComputeRollbackExecutableId(
        uintptr_t image_base) noexcept
    {
        if (!image_base) return 0;
        wchar_t path[32768] {};
        const DWORD path_chars = GetModuleFileNameW(
            nullptr, path, static_cast<DWORD>(std::size(path)));
        if (path_chars == 0 || path_chars >= std::size(path)) return 0;

        struct ScopedFile
        {
            HANDLE value {INVALID_HANDLE_VALUE};
            ~ScopedFile() noexcept
            {
                if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
            }
        } file;
        file.value = CreateFileW(
            path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE
                | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value == INVALID_HANDLE_VALUE) return 0;

        auto read_at = [&](uint64_t offset, void* out, DWORD bytes) noexcept {
            LARGE_INTEGER position {};
            position.QuadPart = static_cast<LONGLONG>(offset);
            DWORD read = 0;
            return SetFilePointerEx(
                       file.value, position, nullptr, FILE_BEGIN)
                && ReadFile(file.value, out, bytes, &read, nullptr)
                && read == bytes;
        };

        IMAGE_DOS_HEADER dos {};
        if (!read_at(0, &dos, sizeof(dos))
            || dos.e_magic != IMAGE_DOS_SIGNATURE
            || dos.e_lfanew <= 0)
        {
            return 0;
        }
        IMAGE_NT_HEADERS64 nt {};
        if (!read_at(static_cast<uint32_t>(dos.e_lfanew), &nt, sizeof(nt))
            || nt.Signature != IMAGE_NT_SIGNATURE
            || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            return 0;
        }
        const uint64_t section_table =
            static_cast<uint32_t>(dos.e_lfanew)
            + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)
            + nt.FileHeader.SizeOfOptionalHeader;

        RollbackHash hash {};
        hash.add_scalar(nt.FileHeader.TimeDateStamp);
        hash.add_scalar(nt.FileHeader.Machine);
        hash.add_scalar(nt.OptionalHeader.SizeOfImage);
        hash.add_scalar(nt.OptionalHeader.SizeOfCode);
        hash.add_scalar(nt.OptionalHeader.AddressOfEntryPoint);
        bool hashed_code = false;
        std::array<uint8_t, 64 * 1024> chunk {};
        for (uint16_t index = 0;
             index < nt.FileHeader.NumberOfSections;
             ++index)
        {
            IMAGE_SECTION_HEADER section {};
            if (!read_at(
                    section_table
                        + static_cast<uint64_t>(index) * sizeof(section),
                    &section,
                    sizeof(section)))
            {
                return 0;
            }
            if ((section.Characteristics & IMAGE_SCN_CNT_CODE) == 0)
                continue;
            if (section.SizeOfRawData == 0) return 0;
            hash.add_scalar(section.VirtualAddress);
            hash.add_scalar(section.Misc.VirtualSize);
            hash.add_scalar(section.SizeOfRawData);
            hash.add_scalar(section.Characteristics);
            uint64_t offset = section.PointerToRawData;
            uint32_t remaining = section.SizeOfRawData;
            while (remaining != 0)
            {
                const DWORD count = static_cast<DWORD>((std::min)(
                    static_cast<size_t>(remaining), chunk.size()));
                if (!read_at(offset, chunk.data(), count)) return 0;
                hash.add_bytes(chunk.data(), count);
                offset += count;
                remaining -= count;
            }
            hashed_code = true;
        }
        return hashed_code ? hash.value : 0;
    }

    static inline bool ValidateRollbackProductionStaticTargets(
        uintptr_t image_base) noexcept
    {
        struct TargetSignature
        {
            uintptr_t rva;
            std::array<uint8_t, 16> bytes;
        };
        static constexpr TargetSignature kTargets[] = {
            {kRollbackProductionRvaPerFrameTick,
             {0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x10,0x49,
              0x89,0x6B,0x18,0x56,0x57,0x41,0x54,0x41}},
            {kRollbackProductionRvaAudioCue,
             {0x41,0x83,0xF8,0x46,0x74,0x68,0x48,0x83,
              0xEC,0x48,0x48,0x8B,0x05,0x9F,0x81,0xDD}},
            {kRollbackProductionRvaAudioSlot,
             {0x48,0x85,0xD2,0x74,0x6E,0x48,0x83,0xEC,
              0x48,0x48,0x8B,0x05,0xC0,0x80,0xDD,0x03}},
            {kRollbackProductionRvaWindParticles,
             {0x48,0x8B,0xC4,0x57,0x48,0x81,0xEC,0xB0,
              0x00,0x00,0x00,0x80,0x3D,0x5E,0x19,0x51}},
            {kRollbackProductionRvaRandU32,
             {0x8B,0x15,0x5E,0xFA,0x50,0x04,0x4C,0x8D,
              0x15,0xF3,0xF9,0x50,0x04,0x83,0xFA,0x19}},
            {kRollbackRVA_ExecMoveChangeAndPost,
             {0x40,0x55,0x48,0x8B,0xEC,0x48,0x83,0xEC,
              0x50,0x48,0xC7,0x45,0xD0,0xFE,0xFF,0xFF}},
            {kRollbackRVA_ExecFinalizeAndPost,
             {0x40,0x55,0x48,0x8B,0xEC,0x48,0x83,0xEC,
              0x50,0x48,0xC7,0x45,0xD0,0xFE,0xFF,0xFF}},
            {kRollbackProductionRvaSetSceneVisibility,
             {0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,
              0x48,0x8D,0x6C,0x24,0xB9,0x48,0x81,0xEC}},
        };
        for (const TargetSignature& target : kTargets)
        {
            std::array<uint8_t, 16> actual {};
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(image_base + target.rva),
                    actual.data(), actual.size())
                || actual != target.bytes)
            {
                return false;
            }
        }
        return true;
    }

#pragma pack(push, 1)
    struct RollbackProductionFrameSummary
    {
        uint64_t epoch {0};
        uint32_t frame {0};
        uint32_t flags {0};
        uint64_t canonical_hash {0};
        uint32_t input[2] {};
    };

    static constexpr uint32_t kRollbackProductionSummaryConfirmed = 1u;
    static constexpr uint32_t kRollbackProductionSummaryAck = 2u;

    static inline uint64_t ComputeRollbackPairEpoch(
        const RollbackProductionConfig& config,
        const RollbackSnapshotManifest& manifest) noexcept
    {
        RollbackHash hash {};
        const uint8_t peer_low = (std::min)(
            config.local_peer, config.remote_peer);
        const uint8_t peer_high = (std::max)(
            config.local_peer, config.remote_peer);
        hash.add_scalar(config.expected_build_id);
        hash.add_scalar(config.expected_schema_id);
        hash.add_scalar(peer_low);
        hash.add_scalar(peer_high);
        hash.add_scalar(config.rollback_window);
        hash.add_scalar(config.input_delay);
        hash.add_scalar(static_cast<uint8_t>(config.lifecycle_mode));
        hash.add_scalar(config.native_input_source_slot);
        hash.add_scalar(
            config.lifecycle_mode == RollbackLifecycleMode::MirroredVersus
                ? config.launch_descriptor.hash()
                : 0);
        hash.add_scalar(manifest.epoch.round_start_digest);
        hash.add_scalar(manifest.epoch.stage_layout_digest);
        return hash.value ? hash.value : 1;
    }

    struct RollbackProductionAudioInvocation
    {
        uint8_t wrapper {0};
        int8_t chara_slot {-1};
        uint8_t reserved[6] {};
        uint64_t unused {0};
        int32_t cue_id {0};
        uint32_t cue_sub_id {0};
    };

    struct RollbackProductionVfxInvocation
    {
        uint8_t request[16] {};
    };

    struct RollbackProductionStagePresentation
    {
        uint8_t kind {0};
        uint8_t reserved[3] {};
        int32_t id {0};
        int32_t scalar {0};
        int32_t endurance {0};
        float fade_timer {0.0f};
        float fade_rate {0.0f};
    };
#pragma pack(pop)

    static_assert(sizeof(RollbackProductionAudioInvocation) == 24);
    static_assert(sizeof(RollbackProductionVfxInvocation) == 16);
    static_assert(sizeof(RollbackProductionStagePresentation) == 24);

    struct RollbackProductionTickArgs
    {
        const uint64_t* input_p1 {nullptr};
        const uint64_t* input_p2 {nullptr};
        const void* camera_input {nullptr};
    };

    class RollbackProductionRuntime
    {
    public:
        using TickFn = void(__fastcall*)(RollbackProductionTickArgs*);
        using AudioFn = void(__fastcall*)(
            uint64_t, int64_t, int32_t, uint32_t);
        using VfxFn = void(__fastcall*)(void*, const void*);
        using WindFn = void(__fastcall*)(void*);
        using RandFn = uint32_t(__fastcall*)();

        static RollbackProductionRuntime& instance() noexcept
        {
            static RollbackProductionRuntime runtime;
            return runtime;
        }

        void configure(const RollbackProductionConfig& config) noexcept
        {
            const bool unchanged_active_configuration =
                RollbackProductionConfigEquivalent(config, m_config)
                && m_status.state != RollbackProductionState::Disabled
                && m_status.state != RollbackProductionState::Fatal
                && m_status.state != RollbackProductionState::Stopping;
            if (unchanged_active_configuration) return;
            if (m_hook_owns_tick)
            {
                // An owned native boundary may only be released after the
                // fail-closed lobby transition. The controller will retry the
                // pending configuration once active PVP has ended.
                fail_closed("operator-reconfigure");
                return;
            }
            shutdown();
            m_status = {};
            try
            {
                m_config = config;
            }
            catch (...)
            {
                m_config.enabled = false;
                m_status.state = RollbackProductionState::Fatal;
                m_status.failure = "production-config-copy-failed";
                return;
            }
            m_local_summaries = {};
            m_remote_summaries = {};
            m_local_summary_valid = {};
            m_remote_summary_valid = {};
            m_summary_consensus.clear();
            m_local_confirmed_input_hash = {};
            m_remote_confirmed_input_hash = {};
            m_camera_history.clear();
            m_pending_pre_activation_failure = nullptr;
            m_status.lifecycle_mode =
                static_cast<uint8_t>(m_config.lifecycle_mode);
            m_status.local_player_slot = m_config.local_player_slot;
            m_status.native_input_source_slot =
                m_config.native_input_source_slot;
            m_status.desired_launch_descriptor_hash =
                m_config.lifecycle_mode
                        == RollbackLifecycleMode::MirroredVersus
                    ? m_config.launch_descriptor.hash()
                    : 0;
            m_status.state = m_config.enabled
                ? RollbackProductionState::WaitingForContext
                : RollbackProductionState::Disabled;
            m_status.failure = m_config.enabled
                ? "waiting-for-context" : "disabled";
            if (!m_config.enabled) return;
            if (!m_config.valid())
            {
                fail_before_activation("invalid-production-config");
                return;
            }
            m_image_base = NativeBinding::imageBase();
            if (!m_image_base)
            {
                m_status.state = RollbackProductionState::WaitingForContext;
                m_status.failure = "waiting-for-image-base";
                return;
            }
            m_status.executable_id = ComputeRollbackExecutableId(m_image_base);
            m_status.executable_match = m_status.executable_id != 0
                && m_status.executable_id == m_config.expected_build_id;
            if (!m_status.executable_match)
            {
                fail_before_activation("executable-fingerprint-mismatch");
                return;
            }
            if (!m_network.start(m_config))
            {
                fail_before_activation("udp-worker-start-failed");
                return;
            }
            m_network_started = true;
            m_status.state = RollbackProductionState::WaitingForPeer;
            m_status.failure = "waiting-for-peer";
        }

        void reject_configuration(const char* failure) noexcept
        {
            if (m_hook_owns_tick)
            {
                fail_closed(failure ? failure : "configuration-rejected");
                return;
            }
            shutdown();
            m_status = {};
            m_status.state = RollbackProductionState::Fatal;
            m_status.failure = failure ? failure : "configuration-rejected";
        }

        void service_game_thread(
            const RollbackSnapshotManifest& manifest) noexcept
        {
            try
            {
#if !HORSE_ENABLE_GEKKONET
            (void)manifest;
            if (m_config.enabled)
                fail_before_activation("gekkonet-disabled");
#else
            if (!m_config.enabled
                || m_status.state == RollbackProductionState::Fatal
                || m_status.state == RollbackProductionState::Stopping)
                return;

            // Once the detour owns the native boundary, every loss of schema,
            // epoch, PVP context, or peer readiness is an in-round failure.
            // Never route an active session through fail_before_activation(),
            // because that unhooks the tick and would resume stock simulation.
            if (m_status.state == RollbackProductionState::Active)
            {
                const RollbackManifestValidationReport active_validation =
                    ValidateRollbackSnapshotManifest(manifest, true);
                const RollbackEpochValidationReport active_epoch =
                    ValidateRollbackLifecycleEpoch(
                        m_manifest.epoch,
                        manifest.epoch,
                        m_config.lifecycle_mode);
                const RollbackUdpWorkerStatus active_network =
                    m_network.status();
                update_network_status(active_network);
                const RollbackProductionActiveGuardReport active_guard =
                    EvaluateRollbackProductionActiveGuard({
                        active_validation.live_ready,
                        manifest.schema_hash() == m_status.schema_id,
                        active_epoch.ok,
                        manifest.epoch.generation
                            == m_manifest.epoch.generation,
                        manifest.epoch.active_for(m_config.lifecycle_mode),
                        active_network.running,
                        active_network.endpoint_open,
                        active_network.endpoint_pinned,
                        active_network.peer_ready,
                        active_network.failure
                            == RollbackUdpWorkerFailure::None,
                        active_network.handshake_generation
                            == m_session_handshake_generation,
                    });
                if (!active_guard.ok)
                    fail_closed(active_guard.failure);
                return;
            }
            if (!m_image_base) m_image_base = NativeBinding::imageBase();
            if (!m_image_base)
            {
                m_status.state = RollbackProductionState::WaitingForContext;
                m_status.failure = "waiting-for-image-base";
                return;
            }
            if (!m_status.executable_match)
            {
                m_status.executable_id =
                    ComputeRollbackExecutableId(m_image_base);
                m_status.executable_match = m_status.executable_id != 0
                    && m_status.executable_id == m_config.expected_build_id;
                if (!m_status.executable_match)
                {
                    fail_before_activation("executable-fingerprint-mismatch");
                    return;
                }
            }
            if (!m_network_started)
            {
                if (!m_network.start(m_config))
                {
                    fail_before_activation("udp-worker-start-failed");
                    return;
                }
                m_network_started = true;
            }
            const RollbackUdpWorkerStatus network = m_network.status();
            update_network_status(network);
            const bool fatal_network_failure =
                network.failure == RollbackUdpWorkerFailure::InvalidConfig
                || network.failure
                    == RollbackUdpWorkerFailure::QueueOverflow
                || network.failure
                    == RollbackUdpWorkerFailure::ResourceAllocationFailed
                || network.failure
                    == RollbackUdpWorkerFailure::NonceGenerationFailed
                || network.failure
                    == RollbackUdpWorkerFailure::PeerSessionChanged;
            const bool transport_ready = network.running
                && network.endpoint_open
                && network.endpoint_pinned
                && network.peer_ready
                && network.handshake_generation != 0
                && network.failure == RollbackUdpWorkerFailure::None;
            m_status.peer_ready = transport_ready;
            if (m_config.lifecycle_mode
                == RollbackLifecycleMode::MirroredVersus)
            {
                if (fatal_network_failure)
                {
                    fail_before_activation("udp-worker-failed");
                    return;
                }
                if (!transport_ready)
                {
                    m_status.state = RollbackProductionState::WaitingForPeer;
                    m_status.failure = !network.running
                        ? "waiting-for-udp-worker"
                        : "waiting-for-coherent-authenticated-peer";
                    return;
                }
                if (!service_launch_barrier_transport(network))
                {
                    fail_before_activation("launch-barrier-transport-failed");
                    return;
                }
            }
            m_status.schema_id = manifest.schema_hash();
            m_status.schema_match = m_status.schema_id != 0
                && m_status.schema_id == m_config.expected_schema_id;
            const RollbackManifestValidationReport validation =
                ValidateRollbackSnapshotManifest(manifest, true);
            m_status.manifest_ready = validation.live_ready;
            m_status.lifecycle_ready = manifest.epoch.active_for(
                m_config.lifecycle_mode);
            if (!m_status.schema_match)
            {
                fail_before_activation("schema-id-mismatch");
                return;
            }
            if (!m_status.manifest_ready || !m_status.lifecycle_ready)
            {
                m_status.state = RollbackProductionState::WaitingForContext;
                m_status.failure = !m_status.manifest_ready
                    ? validation.failure : "waiting-for-active-pvp-epoch";
                return;
            }
            if (fatal_network_failure)
            {
                fail_before_activation("udp-worker-failed");
                return;
            }
            if (!transport_ready)
            {
                if (m_gekko.created())
                {
                    fail_before_activation(
                        "peer-session-lost-before-activation");
                    return;
                }
                m_status.state = RollbackProductionState::WaitingForPeer;
                m_status.failure = !network.running
                    ? "waiting-for-udp-worker"
                    : "waiting-for-coherent-authenticated-peer";
                return;
            }
            m_manifest = manifest;
            if (m_config.lifecycle_mode
                    == RollbackLifecycleMode::MirroredVersus
                && !launch_barrier_ready(
                    RollbackLaunchBarrierStage::BattleBaseline))
            {
                m_status.state =
                    RollbackProductionState::WaitingForLaunchBarrier;
                m_status.failure =
                    "waiting-for-matching-battle-baseline";
                return;
            }
            if (m_pair_epoch == 0)
            {
                m_pair_epoch = ComputeRollbackPairEpoch(
                    m_config, m_manifest);
                m_status.epoch = m_pair_epoch;
            }
            if (!m_gekko.created() && !start_gekko())
            {
                fail_before_activation("gekko-start-failed");
                return;
            }
            if (!m_status.tick_hook_installed && !install_hooks())
            {
                fail_before_activation("production-hook-install-failed");
                return;
            }
            if (m_session_handshake_generation != 0
                && network.handshake_generation
                    != m_session_handshake_generation)
            {
                fail_before_activation(
                    "peer-session-changed-before-activation");
                return;
            }
            if (!m_gekko_session_started)
            {
                pump_gekko_connection();
                if (m_pending_pre_activation_failure)
                {
                    const char* failure =
                        m_pending_pre_activation_failure;
                    m_pending_pre_activation_failure = nullptr;
                    fail_before_activation(failure);
                    return;
                }
                m_status.state = RollbackProductionState::WaitingForGekko;
                m_status.failure = "waiting-for-gekko-session";
                return;
            }
            m_status.gekko_ready = true;
            m_status.state = RollbackProductionState::Active;
            m_status.failure = "ok";
#endif
            }
            catch (...)
            {
                if (m_hook_owns_tick)
                    fail_closed("production-service-exception");
                else
                    fail_before_activation("production-service-exception");
            }
        }

        void shutdown() noexcept
        {
            const bool had_runtime = m_config.enabled || m_gekko.created()
                || m_tick_detour || m_network.status().running;
            stop_callback_admission();
            if (had_runtime)
                m_status.state = RollbackProductionState::Stopping;
            // Stop new native callbacks before joining the network worker.
            m_accept_effects = false;
            unhook(m_tick_detour);
            unhook(m_vfx_detour);
            unhook(m_audio_cue_detour);
            unhook(m_audio_slot_detour);
            unhook(m_wind_detour);
            m_status.tick_hook_installed = false;
            m_status.presentation_hooks_installed = false;
            m_network.stop();
            m_network_started = false;
            m_gekko.shutdown();
            m_store.clear();
            m_ledger.clear();
            m_camera_history.clear();
            m_simulation_thread_id = 0;
            m_post_advance_state = {};
            m_post_advance_frame.clear();
            m_post_advance_epoch = 0;
            m_manifest = {};
            m_current_frame.clear();
            m_effect_frame.clear();
            m_first_advance_frame.clear();
            m_last_summary_published.clear();
            m_pending_effect_confirmation.clear();
            m_gekko_frame_high_water.clear();
            m_gekko_update_calls = 0;
            m_local_summaries = {};
            m_remote_summaries = {};
            m_local_summary_valid = {};
            m_remote_summary_valid = {};
            m_summary_consensus.clear();
            m_pending_pre_activation_failure = nullptr;
            m_local_launch_barrier = {};
            m_remote_launch_barrier = {};
            m_local_launch_barrier_valid = false;
            m_remote_launch_barrier_valid = false;
            m_launch_barrier_generation = 0;
            m_launch_barrier_service_ticks = 0;
            m_gekko_session_started = false;
            m_session_handshake_generation = 0;
            m_hook_owns_tick = false;
            m_pair_epoch = 0;
            m_tick_trampoline = 0;
            m_audio_cue_trampoline = 0;
            m_audio_slot_trampoline = 0;
            m_vfx_trampoline = 0;
            m_wind_trampoline = 0;
            m_config.enabled = false;
            if (had_runtime)
            {
                m_status.state = RollbackProductionState::Disabled;
                m_status.failure = "stopped";
            }
        }

        const RollbackProductionStatus& status() const noexcept
        {
            return m_status;
        }

        bool active() const noexcept
        {
            return m_status.state == RollbackProductionState::Active;
        }

        bool owns_tick_boundary() const noexcept
        {
            return m_hook_owns_tick;
        }

        bool publish_launch_barrier(
            const RollbackLaunchBarrierMessage& message) noexcept
        {
            if (m_config.lifecycle_mode
                    != RollbackLifecycleMode::MirroredVersus
                || !RollbackLaunchBarrierValid(message)
                || message.local_player_slot
                    != m_config.local_player_slot
                || message.seed != m_config.launch_descriptor.seed
                || message.desired_descriptor_hash
                    != m_config.launch_descriptor.hash())
            {
                return false;
            }
            const RollbackUdpWorkerStatus network = m_network.status();
            if (!network.peer_ready
                || network.failure != RollbackUdpWorkerFailure::None
                || network.handshake_generation == 0)
            {
                return false;
            }
            if (m_local_launch_barrier_valid
                && static_cast<uint8_t>(message.stage)
                    < static_cast<uint8_t>(m_local_launch_barrier.stage))
            {
                return false;
            }
            m_local_launch_barrier = message;
            m_local_launch_barrier_valid = true;
            m_launch_barrier_generation = network.handshake_generation;
            update_launch_barrier_status();
            return enqueue_local_launch_barrier(network.handshake_generation);
        }

        bool launch_barrier_ready(
            RollbackLaunchBarrierStage stage) const noexcept
        {
            return m_local_launch_barrier_valid
                && m_remote_launch_barrier_valid
                && m_local_launch_barrier.stage == stage
                && m_remote_launch_barrier.stage == stage
                && RollbackLaunchBarriersMatch(
                    m_local_launch_barrier, m_remote_launch_barrier);
        }

        bool peer_launch_barrier_at_least(
            RollbackLaunchBarrierStage stage) const noexcept
        {
            return m_remote_launch_barrier_valid
                && static_cast<uint8_t>(m_remote_launch_barrier.stage)
                    >= static_cast<uint8_t>(stage);
        }

        bool arm_mirrored_launch_boundary() noexcept
        {
            if (m_config.lifecycle_mode
                    != RollbackLifecycleMode::MirroredVersus
                || !launch_barrier_ready(
                    RollbackLaunchBarrierStage::SetupApplied))
            {
                return false;
            }
            const RollbackUdpWorkerStatus network = m_network.status();
            if (!network.peer_ready
                || network.failure != RollbackUdpWorkerFailure::None
                || network.handshake_generation == 0)
            {
                return false;
            }
            m_session_handshake_generation =
                network.handshake_generation;
            if (!install_tick_hook_only()) return false;
            m_status.state =
                RollbackProductionState::WaitingForLaunchBarrier;
            m_status.failure = "mirrored-launch-boundary-armed";
            return true;
        }

        bool capture_and_publish_mirrored_baseline() noexcept
        {
            if (m_config.lifecycle_mode
                    != RollbackLifecycleMode::MirroredVersus
                || !m_hook_owns_tick
                || !m_manifest.epoch.active_for(
                    RollbackLifecycleMode::MirroredVersus))
            {
                return false;
            }
            RollbackStepState state {};
            const RollbackStepStateReport captured =
                CaptureRollbackStepState(
                    m_image_base,
                    m_manifest,
                    state,
                    RollbackLifecycleMode::MirroredVersus);
            if (!captured.ok
                || state.frame_counter >
                    static_cast<uint32_t>(INT32_MAX)
                || state.canonical_hash == 0)
            {
                fail_closed(captured.ok
                    ? "mirrored-baseline-frame-invalid"
                    : captured.failure);
                return false;
            }
            RollbackLaunchBarrierMessage message {};
            message.stage =
                RollbackLaunchBarrierStage::BattleBaseline;
            message.lifecycle_mode =
                RollbackLifecycleMode::MirroredVersus;
            message.local_player_slot = m_config.local_player_slot;
            message.seed = m_config.launch_descriptor.seed;
            message.baseline_frame =
                static_cast<int32_t>(state.frame_counter);
            message.canonical_stage_identity =
                m_config.launch_descriptor.stage;
            message.desired_descriptor_hash =
                m_config.launch_descriptor.hash();
            message.observed_descriptor_hash =
                m_status.observed_launch_descriptor_hash;
            message.epoch = ComputeRollbackPairEpoch(
                m_config, m_manifest);
            message.canonical_baseline_hash = state.canonical_hash;
            return publish_launch_barrier(message);
        }

        void request_fail_closed(const char* reason) noexcept
        {
            if (m_hook_owns_tick) fail_closed(reason);
            else fail_before_activation(reason ? reason
                                               : "mirrored-launch-aborted");
        }

        void record_lobby_return_dispatch(bool succeeded) noexcept
        {
            m_status.lobby_return_dispatched = true;
            m_status.lobby_return_succeeded = succeeded;
        }

    private:
        RollbackProductionRuntime() = default;
        ~RollbackProductionRuntime() noexcept { shutdown(); }
        RollbackProductionRuntime(const RollbackProductionRuntime&) = delete;
        RollbackProductionRuntime& operator=(
            const RollbackProductionRuntime&) = delete;

        void update_launch_barrier_status() noexcept
        {
            m_status.launch_setup_local = m_local_launch_barrier_valid
                && m_local_launch_barrier.stage
                    == RollbackLaunchBarrierStage::SetupApplied;
            m_status.launch_setup_peer = m_remote_launch_barrier_valid
                && m_remote_launch_barrier.stage
                    == RollbackLaunchBarrierStage::SetupApplied;
            m_status.launch_baseline_local = m_local_launch_barrier_valid
                && m_local_launch_barrier.stage
                    == RollbackLaunchBarrierStage::BattleBaseline;
            m_status.launch_baseline_peer = m_remote_launch_barrier_valid
                && m_remote_launch_barrier.stage
                    == RollbackLaunchBarrierStage::BattleBaseline;
            m_status.launch_barrier_ready =
                launch_barrier_ready(
                    m_status.launch_baseline_local
                        ? RollbackLaunchBarrierStage::BattleBaseline
                        : RollbackLaunchBarrierStage::SetupApplied);
            m_status.observed_launch_descriptor_hash =
                m_local_launch_barrier_valid
                    ? m_local_launch_barrier.observed_descriptor_hash
                    : 0;
            m_status.peer_launch_descriptor_hash =
                m_remote_launch_barrier_valid
                    ? m_remote_launch_barrier.observed_descriptor_hash
                    : 0;
            m_status.launch_baseline_hash =
                m_status.launch_baseline_local
                    ? m_local_launch_barrier.canonical_baseline_hash
                    : 0;
            m_status.peer_launch_baseline_hash =
                m_status.launch_baseline_peer
                    ? m_remote_launch_barrier.canonical_baseline_hash
                    : 0;
            m_status.launch_baseline_epoch =
                m_status.launch_baseline_local
                    ? m_local_launch_barrier.epoch : 0;
            m_status.peer_launch_baseline_epoch =
                m_status.launch_baseline_peer
                    ? m_remote_launch_barrier.epoch : 0;
            m_status.launch_stage_identity =
                m_local_launch_barrier_valid
                    ? m_local_launch_barrier.canonical_stage_identity : 0;
            m_status.peer_launch_stage_identity =
                m_remote_launch_barrier_valid
                    ? m_remote_launch_barrier.canonical_stage_identity : 0;
            m_status.launch_baseline_frame =
                m_status.launch_baseline_local
                    ? m_local_launch_barrier.baseline_frame
                    : -1;
        }

        bool enqueue_local_launch_barrier(uint64_t generation) noexcept
        {
            return m_local_launch_barrier_valid
                && generation != 0
                && m_network.enqueue(
                    RollbackProtocolV2PacketType::LaunchBarrier,
                    &m_local_launch_barrier,
                    sizeof(m_local_launch_barrier),
                    {},
                    generation);
        }

        bool accept_launch_barrier_message(
            const RollbackUdpMessage& message) noexcept
        {
            if (message.payload_bytes
                    != sizeof(RollbackLaunchBarrierMessage)
                || message.handshake_generation
                    != m_launch_barrier_generation)
            {
                return false;
            }
            RollbackLaunchBarrierMessage barrier {};
            std::memcpy(
                &barrier, message.payload.data(), sizeof(barrier));
            if (!RollbackLaunchBarrierValid(barrier)
                || barrier.local_player_slot
                    != static_cast<uint8_t>(
                        1u - m_config.local_player_slot)
                || barrier.seed != m_config.launch_descriptor.seed
                || barrier.desired_descriptor_hash
                    != m_config.launch_descriptor.hash()
                || (m_remote_launch_barrier_valid
                    && static_cast<uint8_t>(barrier.stage)
                        < static_cast<uint8_t>(
                            m_remote_launch_barrier.stage)))
            {
                return false;
            }
            m_remote_launch_barrier = barrier;
            m_remote_launch_barrier_valid = true;
            update_launch_barrier_status();
            return true;
        }

        bool service_launch_barrier_transport(
            const RollbackUdpWorkerStatus& network) noexcept
        {
            if (m_config.lifecycle_mode
                != RollbackLifecycleMode::MirroredVersus)
            {
                return true;
            }
            ++m_launch_barrier_service_ticks;
            if (m_launch_barrier_generation
                != network.handshake_generation)
            {
                m_launch_barrier_generation =
                    network.handshake_generation;
                m_remote_launch_barrier = {};
                m_remote_launch_barrier_valid = false;
                update_launch_barrier_status();
                if (m_local_launch_barrier_valid
                    && !enqueue_local_launch_barrier(
                        m_launch_barrier_generation))
                {
                    return false;
                }
            }

            RollbackUdpMessage message {};
            while (m_network.dequeue(message))
            {
                if (message.handshake_generation
                    != m_launch_barrier_generation)
                {
                    return false;
                }
                if (message.packet_type
                    == RollbackProtocolV2PacketType::LaunchBarrier)
                {
                    if (!accept_launch_barrier_message(message))
                        return false;
                    continue;
                }
                if (message.packet_type
                        == RollbackProtocolV2PacketType::Disconnect
                    || message.packet_type
                        == RollbackProtocolV2PacketType::Desync)
                {
                    return false;
                }
                // Gekko and summary traffic before the battle baseline means
                // the peer released simulation before the launch contract.
                return false;
            }
            if (m_local_launch_barrier_valid
                && (m_launch_barrier_service_ticks % 30u) == 0u
                && !enqueue_local_launch_barrier(
                    m_launch_barrier_generation))
            {
                return false;
            }
            return true;
        }

#if HORSE_ENABLE_GEKKONET
        static bool gekko_core_send(
            void* context,
            uint8_t remote_peer,
            const void* data,
            uint16_t bytes) noexcept
        {
            auto* runtime = static_cast<RollbackProductionRuntime*>(context);
            if (!runtime || remote_peer != runtime->m_config.remote_peer)
                return false;
            const RollbackUdpWorkerStatus network =
                runtime->m_network.status();
            return network.handshake_generation
                    == runtime->m_session_handshake_generation
                && runtime->m_network.enqueue(
                    RollbackProtocolV2PacketType::Gekko,
                    data,
                    bytes,
                    {},
                    runtime->m_session_handshake_generation);
        }

        static RollbackGekkoReceiveStatus gekko_core_receive(
            void* context,
            RollbackGekkoDatagram& out) noexcept
        {
            auto* runtime = static_cast<RollbackProductionRuntime*>(context);
            if (!runtime) return RollbackGekkoReceiveStatus::Fatal;
            RollbackUdpMessage message {};
            while (runtime->m_network.dequeue(message))
            {
                if (message.handshake_generation
                    != runtime->m_session_handshake_generation)
                {
                    runtime->fail_closed(
                        "udp-generation-changed-during-receive");
                    return RollbackGekkoReceiveStatus::Fatal;
                }
                if (message.packet_type
                    == RollbackProtocolV2PacketType::LaunchBarrier)
                {
                    if (!runtime->accept_launch_barrier_message(message))
                    {
                        runtime->fail_closed("launch-barrier-invalid");
                        return RollbackGekkoReceiveStatus::Fatal;
                    }
                    continue;
                }
                if (message.packet_type
                    == RollbackProtocolV2PacketType::Input)
                {
                    if (runtime->m_status.state
                        == RollbackProductionState::Active)
                        runtime->accept_peer_summary(message);
                    continue;
                }
                if (message.packet_type
                        == RollbackProtocolV2PacketType::Disconnect
                    || message.packet_type
                        == RollbackProtocolV2PacketType::Desync)
                {
                    runtime->fail_closed("peer-terminated-session");
                    return RollbackGekkoReceiveStatus::Fatal;
                }
                if (message.packet_type
                        != RollbackProtocolV2PacketType::Gekko
                    || message.payload_bytes == 0)
                    continue;
                out.remote_peer = runtime->m_config.remote_peer;
                out.bytes = message.payload_bytes;
                std::memcpy(out.payload.data(), message.payload.data(),
                            message.payload_bytes);
                return RollbackGekkoReceiveStatus::Packet;
            }
            return RollbackGekkoReceiveStatus::Empty;
        }

        static bool gekko_core_game_event(
            void* context,
            GekkoGameEvent& event,
            const void* auxiliary) noexcept
        {
            auto* runtime = static_cast<RollbackProductionRuntime*>(context);
            return runtime
                && runtime->process_game_event(event, auxiliary);
        }

        static bool gekko_core_idle_update(void* context) noexcept
        {
            auto* runtime = static_cast<RollbackProductionRuntime*>(context);
            return runtime && runtime->send_oldest_unacknowledged_summary();
        }

        static void gekko_core_failure(
            void* context,
            const char* reason) noexcept
        {
            if (auto* runtime =
                    static_cast<RollbackProductionRuntime*>(context))
                runtime->fail_closed(reason);
        }

        bool start_gekko() noexcept
        {
            m_gekko_frame_high_water.clear();
            m_gekko_update_calls = 0;
            m_session_handshake_generation =
                m_network.status().handshake_generation;
            if (m_session_handshake_generation == 0) return false;
            RollbackGekkoRuntimeConfig config {};
            config.local_player_slot = m_config.local_player_slot;
            config.remote_peer = m_config.remote_peer;
            config.rollback_window = m_config.rollback_window;
            config.input_delay = m_config.input_delay;
            config.state_size = sizeof(RollbackSnapshotHandle);
            RollbackGekkoRuntimeCallbacks callbacks {};
            callbacks.context = this;
            callbacks.send = &gekko_core_send;
            callbacks.receive = &gekko_core_receive;
            callbacks.game_event = &gekko_core_game_event;
            callbacks.idle_update = &gekko_core_idle_update;
            callbacks.failure = &gekko_core_failure;
            return m_gekko.start(config, callbacks);
        }

        void pump_gekko_connection() noexcept
        {
            if (!m_gekko.created()) return;
            if (!m_gekko.poll())
            {
                if (m_status.state != RollbackProductionState::Fatal)
                    fail_closed(m_gekko.failure_reason()[0]
                        ? m_gekko.failure_reason()
                        : "gekko-network-poll-failed");
                return;
            }
            m_gekko_session_started = m_gekko.session_started();
        }
#endif

        bool install_hooks() noexcept
        {
            return install_tick_hook_only()
                && install_presentation_hooks();
        }

        bool install_tick_hook_only() noexcept
        {
            if (m_tick_detour)
                return m_hook_owns_tick
                    && m_status.tick_hook_installed;
            if (!ValidateRollbackProductionStaticTargets(m_image_base))
                return false;
            static constexpr uint8_t kExpectedTickEntry[7] = {
                0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10,
            };
            uint8_t actual[sizeof(kExpectedTickEntry)] {};
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(
                        m_image_base + kRollbackProductionRvaPerFrameTick),
                    actual, sizeof(actual))
                || std::memcmp(actual, kExpectedTickEntry, sizeof(actual)) != 0)
                return false;

            m_callbacks_accepting.store(true, std::memory_order_release);
            s_callback_runtime.store(this, std::memory_order_release);
            if (!install_one(m_tick_detour,
                    m_image_base + kRollbackProductionRvaPerFrameTick,
                    reinterpret_cast<uint64_t>(&tick_hook), m_tick_trampoline))
            {
                stop_callback_admission();
                return false;
            }
            m_status.tick_hook_installed = true;
            m_hook_owns_tick = true;
            m_accept_effects = false;
            return true;
        }

        bool install_presentation_hooks() noexcept
        {
            if (m_status.presentation_hooks_installed)
                return true;
            if (!m_tick_detour || !m_hook_owns_tick)
                return false;
            if (!install_one(m_audio_cue_detour,
                    m_image_base + kRollbackProductionRvaAudioCue,
                    reinterpret_cast<uint64_t>(&audio_cue_hook),
                    m_audio_cue_trampoline)
                || !install_one(m_audio_slot_detour,
                    m_image_base + kRollbackProductionRvaAudioSlot,
                    reinterpret_cast<uint64_t>(&audio_slot_hook),
                    m_audio_slot_trampoline)
                || !install_one(m_wind_detour,
                    m_image_base + kRollbackProductionRvaWindParticles,
                    reinterpret_cast<uint64_t>(&wind_hook), m_wind_trampoline)
                || !install_vfx_dispatcher_hook())
            {
                // Keep the already-owned tick boundary frozen.  Releasing it
                // here would resume stock simulation after launch ownership.
                m_accept_effects = false;
                unhook(m_vfx_detour);
                unhook(m_audio_cue_detour);
                unhook(m_audio_slot_detour);
                unhook(m_wind_detour);
                m_audio_cue_trampoline = 0;
                m_audio_slot_trampoline = 0;
                m_vfx_trampoline = 0;
                m_wind_trampoline = 0;
                m_status.presentation_hooks_installed = false;
                return false;
            }
            m_status.presentation_hooks_installed = true;
            m_accept_effects = true;
            return true;
        }

        bool install_vfx_dispatcher_hook() noexcept
        {
            void* dispatcher_raw = nullptr;
            if (!SafeReadPtr(reinterpret_cast<const void*>(
                    m_image_base + kRollbackProductionRvaVfxDispatcher),
                    &dispatcher_raw) || !dispatcher_raw)
                return false;
            void* vtable_raw = nullptr;
            if (!SafeReadPtr(dispatcher_raw, &vtable_raw) || !vtable_raw)
                return false;
            void* method_raw = nullptr;
            if (!SafeReadPtr(static_cast<const uint8_t*>(vtable_raw) + 0xB8,
                             &method_raw) || !method_raw)
                return false;
            MEMORY_BASIC_INFORMATION memory {};
            if (VirtualQuery(method_raw, &memory, sizeof(memory))
                    != sizeof(memory)
                || memory.AllocationBase
                    != reinterpret_cast<const void*>(m_image_base)
                || (memory.Protect
                    & (PAGE_EXECUTE | PAGE_EXECUTE_READ
                       | PAGE_EXECUTE_READWRITE
                       | PAGE_EXECUTE_WRITECOPY)) == 0)
            {
                return false;
            }
            return install_one(m_vfx_detour,
                reinterpret_cast<uint64_t>(method_raw),
                reinterpret_cast<uint64_t>(&vfx_hook), m_vfx_trampoline);
        }

        static bool install_one(
            std::unique_ptr<PLH::x64Detour>& detour,
            uint64_t target,
            uint64_t callback,
            uint64_t& trampoline) noexcept
        {
            trampoline = 0;
            try
            {
                detour = std::make_unique<PLH::x64Detour>(
                    target, callback, &trampoline);
                if (!detour->hook())
                {
                    detour.reset();
                    trampoline = 0;
                    return false;
                }
                return trampoline != 0;
            }
            catch (...)
            {
                detour.reset();
                trampoline = 0;
                return false;
            }
        }

        static void unhook(std::unique_ptr<PLH::x64Detour>& detour) noexcept
        {
            if (!detour) return;
            try { (void)detour->unHook(); } catch (...) {}
            detour.reset();
        }

        bool try_enter_callback() noexcept
        {
            if (!m_callbacks_accepting.load(std::memory_order_acquire))
                return false;
            m_inflight_callbacks.fetch_add(1, std::memory_order_acq_rel);
            if (!m_callbacks_accepting.load(std::memory_order_acquire))
            {
                m_inflight_callbacks.fetch_sub(1, std::memory_order_acq_rel);
                return false;
            }
            return true;
        }

        void leave_callback() noexcept
        {
            m_inflight_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }

        void stop_callback_admission() noexcept
        {
            m_callbacks_accepting.store(false, std::memory_order_release);
            s_callback_runtime.store(nullptr, std::memory_order_release);
            while (m_inflight_callbacks.load(std::memory_order_acquire) != 0)
                std::this_thread::yield();
        }

        class CallbackLease
        {
        public:
            explicit CallbackLease(
                RollbackProductionRuntime* runtime) noexcept
                : m_runtime(runtime)
                , m_entered(runtime && runtime->try_enter_callback())
            {
            }

            ~CallbackLease() noexcept
            {
                if (m_entered) m_runtime->leave_callback();
            }

            explicit operator bool() const noexcept { return m_entered; }

        private:
            RollbackProductionRuntime* m_runtime {nullptr};
            bool m_entered {false};
        };

        static void __fastcall tick_hook(RollbackProductionTickArgs* args)
        {
            RollbackProductionRuntime* runtime =
                s_callback_runtime.load(std::memory_order_acquire);
            CallbackLease lease(runtime);
            if (!lease) return;
            runtime->on_tick(args);
        }

        void on_tick(RollbackProductionTickArgs* args) noexcept
        {
            TickFn original = reinterpret_cast<TickFn>(m_tick_trampoline);
            if (!m_hook_owns_tick)
            {
                if (original) original(args);
                return;
            }
            const DWORD current_thread = GetCurrentThreadId();
            if (m_simulation_thread_id == 0)
                m_simulation_thread_id = current_thread;
            else if (m_simulation_thread_id != current_thread)
            {
                fail_closed("production-tick-thread-changed");
                return;
            }
            if (m_status.state != RollbackProductionState::Active
                && m_status.state != RollbackProductionState::WaitingForGekko)
            {
                // Once production owns the boundary, fatal/stopping states
                // freeze the round. Never fall back to stock simulation.
                return;
            }
            if (!args || !original)
            {
                fail_closed("invalid-per-frame-hook-args");
                return;
            }
            if (!RollbackGekkoMayUpdateSession(
                    m_gekko_frame_high_water, m_gekko_update_calls))
            {
                fail_closed("gekko-signed-frame-ceiling-reached");
                return;
            }
            RollbackLifecycleEpoch live {};
            if (!CaptureRollbackLifecycleEpoch(m_image_base, live))
            {
                fail_closed("lifecycle-capture-failed");
                return;
            }
            live.generation = m_manifest.epoch.generation;
            const RollbackEpochValidationReport epoch =
                ValidateRollbackLifecycleEpoch(
                    m_manifest.epoch, live, m_config.lifecycle_mode);
            if (!epoch.ok)
            {
                fail_closed(epoch.failure);
                return;
            }
            const RollbackUdpWorkerStatus network = m_network.status();
            if (!network.running
                || !network.endpoint_open
                || !network.endpoint_pinned
                || !network.peer_ready
                || network.failure != RollbackUdpWorkerFailure::None
                || network.handshake_generation
                    != m_session_handshake_generation)
            {
                fail_closed("peer-readiness-lost");
                return;
            }

#if HORSE_ENABLE_GEKKONET
            const uint64_t* source =
                m_config.native_input_source_slot == 0
                ? args->input_p1 : args->input_p2;
            uint64_t native_input = 0;
            if (!source || !SafeReadBytes(source, &native_input,
                                          sizeof(native_input)))
            {
                fail_closed("local-input-read-failed");
                return;
            }
            uint32_t local_input = static_cast<uint32_t>(native_input);
            bool events_ok = m_gekko.update(
                local_input, args->camera_input);
            m_gekko_update_calls = m_gekko.update_calls();
            const RollbackUdpWorkerStatus post_update_network =
                m_network.status();
            if (!post_update_network.peer_ready
                || post_update_network.failure
                    != RollbackUdpWorkerFailure::None
                || post_update_network.handshake_generation
                    != m_session_handshake_generation)
            {
                fail_closed("peer-generation-changed-during-update");
                return;
            }
            if (!events_ok
                && (m_status.state == RollbackProductionState::Active
                    || m_status.state
                        == RollbackProductionState::WaitingForGekko))
                fail_closed("gekko-event-batch-failed");
            const RollbackUdpWorkerStatus post_event_network =
                m_network.status();
            if (events_ok
                && (!post_event_network.running
                    || !post_event_network.endpoint_open
                    || !post_event_network.endpoint_pinned
                    || !post_event_network.peer_ready
                    || post_event_network.failure
                        != RollbackUdpWorkerFailure::None
                    || post_event_network.handshake_generation
                        != m_session_handshake_generation))
            {
                fail_closed("peer-generation-changed-during-event-batch");
                events_ok = false;
            }
            // Presentation commits can call native dispatchers. Defer them
            // until every Load/Advance/Save in this Gekko batch has completed
            // so a post-Advance cached save exactly matches live state.
            if (events_ok
                && m_status.state == RollbackProductionState::Active
                && m_pending_effect_confirmation.valid)
            {
                const uint32_t confirmed =
                    m_pending_effect_confirmation.value;
                m_pending_effect_confirmation.clear();
                if (!m_ledger.confirm_through(
                        m_manifest.epoch.generation,
                        confirmed, &commit_side_effect, this)
                    || !m_ledger.report().ok)
                {
                    fail_closed(m_ledger.report().failure);
                }
                update_presentation_status();
            }
#else
            (void)args;
#endif
        }

#if HORSE_ENABLE_GEKKONET
        bool process_game_event(
            GekkoGameEvent& event,
            const void* camera_input) noexcept
        {
            if (m_status.state != RollbackProductionState::Active
                && m_status.state != RollbackProductionState::WaitingForGekko)
                return false;
            switch (event.type)
            {
            case GekkoSaveEvent:
                return process_save(event);
            case GekkoLoadEvent:
                return process_load(event);
            case GekkoAdvanceEvent:
                return process_advance(event, camera_input);
            default:
                return true;
            }
        }

        bool process_save(GekkoGameEvent& event) noexcept
        {
            if (!event.data.save.state
                || !event.data.save.state_len || !event.data.save.checksum)
            {
                fail_closed("invalid-gekko-save-event");
                return false;
            }
            uint32_t frame = 0;
            if (!RollbackGekkoStateFrameToKey(
                    event.data.save.frame, frame))
            {
                fail_closed("gekko-signed-frame-ceiling-reached");
                return false;
            }
            const bool baseline = event.data.save.frame
                == kRollbackGekkoBaselineFrame;
            if (!baseline)
            {
                RollbackObserveGekkoFrame(
                    m_gekko_frame_high_water, frame);
            }
            if ((!baseline && (!m_current_frame.valid
                              || m_current_frame.value != frame))
                || (baseline && m_current_frame.valid
                    && m_current_frame.value != frame))
            {
                fail_closed("gekko-save-live-frame-mismatch");
                return false;
            }
            RollbackStepState state {};
            if (m_post_advance_frame.valid
                && m_post_advance_frame.value == frame
                && m_post_advance_epoch == m_manifest.epoch.generation)
            {
                state = std::move(m_post_advance_state);
            }
            else
            {
                const RollbackStepStateReport captured =
                    CaptureRollbackStepState(
                        m_image_base,
                        m_manifest,
                        state,
                        m_config.lifecycle_mode);
                if (!captured.ok)
                {
                    fail_closed(captured.failure);
                    return false;
                }
            }
            m_post_advance_state = {};
            m_post_advance_frame.clear();
            m_post_advance_epoch = 0;
            if (!ValidateRollbackStepStateIntegrity(state))
            {
                fail_closed("gekko-save-state-integrity-invalid");
                return false;
            }
            RollbackSnapshotHandle handle {};
            const RollbackSnapshotStoreReport saved = m_store.save(
                m_manifest.epoch.generation,
                frame,
                state.combined_hash,
                state.canonical_hash,
                std::move(state),
                m_current_frame.valid ? m_current_frame
                                      : RollbackFrameStamp::From(frame),
                m_config.rollback_window,
                handle);
            if (!saved.ok)
            {
                fail_closed(saved.failure);
                return false;
            }
            std::memcpy(event.data.save.state, &handle, sizeof(handle));
            *event.data.save.state_len = sizeof(handle);
            *event.data.save.checksum = static_cast<uint32_t>(
                handle.canonical_hash ^ (handle.canonical_hash >> 32));
            ++m_status.saves;
            return true;
        }

        bool process_load(GekkoGameEvent& event) noexcept
        {
            if (event.data.load.state_len
                    != sizeof(RollbackSnapshotHandle)
                || !event.data.load.state)
            {
                fail_closed("invalid-gekko-load-event");
                return false;
            }
            uint32_t frame = 0;
            if (!RollbackGekkoStateFrameToKey(
                    event.data.load.frame, frame))
            {
                fail_closed("gekko-signed-frame-ceiling-reached");
                return false;
            }
            if (event.data.load.frame != kRollbackGekkoBaselineFrame)
                RollbackObserveGekkoFrame(m_gekko_frame_high_water, frame);
            RollbackSnapshotHandle handle {};
            std::memcpy(&handle, event.data.load.state, sizeof(handle));
            if (handle.frame != frame)
            {
                fail_closed("gekko-load-frame-handle-mismatch");
                return false;
            }
            const RollbackStepState* state = nullptr;
            const RollbackSnapshotStoreReport loaded = m_store.load(
                handle, state);
            if (!loaded.ok || !state
                || handle.epoch != m_manifest.epoch.generation)
            {
                fail_closed(!loaded.ok ? loaded.failure
                    : "gekko-load-epoch-mismatch");
                return false;
            }
            // Save F is captured after Advance F. Load F resumes at F+1, so
            // frame-F effects belong to the restored state and must survive.
            m_ledger.rollback_after(handle.epoch, handle.frame);
            m_post_advance_state = {};
            m_post_advance_frame.clear();
            m_post_advance_epoch = 0;
            const RollbackStepStateReport restored = RestoreRollbackStepState(
                m_image_base, *state, true, m_config.lifecycle_mode);
            if (!restored.ok)
            {
                fail_closed(restored.failure);
                return false;
            }
            RollbackStepState verified {};
            const RollbackStepStateReport verification =
                CaptureRollbackStepState(
                    m_image_base, m_manifest, verified,
                    m_config.lifecycle_mode);
            if (!verification.ok
                || verified.canonical_hash != handle.canonical_hash)
            {
                fail_closed(!verification.ok
                    ? verification.failure
                    : "gekko-load-canonical-restore-mismatch");
                return false;
            }
            m_status.last_restore_expected_hash = handle.canonical_hash;
            m_status.last_restore_observed_hash = verified.canonical_hash;
            m_status.final_restore_verified = true;
            if (event.data.load.frame == kRollbackGekkoBaselineFrame)
                m_status.baseline_restore_verified = true;
            else
                m_status.prediction_restore_verified = true;
            m_current_frame = handle.frame;
            ++m_status.loads;
            return true;
        }

        bool process_advance(
            GekkoGameEvent& event,
            const void* camera_input) noexcept
        {
            if (event.data.adv.frame >= 0
                && !RollbackGekkoFrameIsProductionSafe(
                    event.data.adv.frame))
            {
                fail_closed("gekko-signed-frame-ceiling-reached");
                return false;
            }
            const RollbackGekkoGameplayInputDecodeReport decoded =
                DecodeRollbackGekkoGameplayInputs(
                    event.data.adv.frame,
                    event.data.adv.inputs,
                    event.data.adv.input_len,
                    2);
            if (!decoded.ok)
            {
                fail_closed(decoded.failure);
                return false;
            }
            RollbackObserveGekkoFrame(
                m_gekko_frame_high_water, decoded.frame);
            RollbackDecodedGameplayInput p0 {};
            RollbackDecodedGameplayInput p1 {};
            if (!GetRollbackGekkoDecodedGameplayInput(decoded, 0, p0)
                || !GetRollbackGekkoDecodedGameplayInput(decoded, 1, p1))
            {
                fail_closed("gekko-player-input-missing");
                return false;
            }
            uint64_t native_input[2] {p0.input_value, p1.input_value};
            using CameraHistory = RollbackHistoricalCameraArgs<128>;
            CameraHistory::Bytes intercepted_camera {};
            const CameraHistory::Bytes* intercepted = nullptr;
            if (!event.data.adv.rolling_back)
            {
                if (!camera_input
                    || !SafeReadBytes(camera_input,
                        intercepted_camera.data(),
                        intercepted_camera.size()))
                {
                    fail_closed("camera-argument-read-failed");
                    return false;
                }
                intercepted = &intercepted_camera;
            }
            CameraHistory::Bytes historical_camera {};
            RollbackProductionTickArgs temporary {
                &native_input[0], &native_input[1], historical_camera.data(),
            };
            const uint32_t frame = static_cast<uint32_t>(event.data.adv.frame);
            const RollbackHistoricalCameraArgsReport camera =
                m_camera_history.select(
                    m_manifest.epoch.generation,
                    frame,
                    event.data.adv.rolling_back,
                    intercepted,
                    historical_camera);
            if (!camera.ok)
            {
                switch (camera.failure)
                {
                case RollbackHistoricalCameraArgsFailure::MissingRollbackFrame:
                    fail_closed("historical-camera-argument-missing");
                    break;
                case RollbackHistoricalCameraArgsFailure::RetentionCollision:
                    fail_closed("historical-camera-retention-collision");
                    break;
                case RollbackHistoricalCameraArgsFailure::IntegrityMismatch:
                    fail_closed("historical-camera-integrity-mismatch");
                    break;
                default:
                    fail_closed("historical-camera-invalid-argument");
                    break;
                }
                return false;
            }
            m_effect_epoch = m_manifest.epoch.generation;
            m_effect_frame = frame;
            m_effect_ordinal = 0;
            if (event.data.adv.rolling_back)
            {
                RollbackResimScope scope(m_effect_epoch, frame);
                reinterpret_cast<TickFn>(m_tick_trampoline)(&temporary);
                ++m_status.rollback_advances;
            }
            else
            {
                reinterpret_cast<TickFn>(m_tick_trampoline)(&temporary);
            }
            if (m_status.state != RollbackProductionState::Active
                && m_status.state != RollbackProductionState::WaitingForGekko)
            {
                m_effect_frame.clear();
                return false;
            }
            m_current_frame = frame;
            ++m_status.advances;

            RollbackStepState final_state {};
            const RollbackStepStateReport captured = CaptureRollbackStepState(
                m_image_base,
                m_manifest,
                final_state,
                m_config.lifecycle_mode);
            if (!captured.ok)
            {
                m_effect_frame.clear();
                fail_closed(captured.failure);
                return false;
            }
            if (!queue_stage_presentation(final_state.breakable_stage))
            {
                m_effect_frame.clear();
                fail_closed(m_ledger.report().failure);
                return false;
            }
            m_effect_frame.clear();
            RollbackProductionFrameSummary summary {};
            summary.epoch = m_pair_epoch;
            summary.frame = frame;
            summary.canonical_hash = final_state.canonical_hash;
            summary.input[0] = p0.input_value;
            summary.input[1] = p1.input_value;
            const size_t summary_slot = frame & 127u;
            if ((m_local_summary_valid[summary_slot]
                    && m_local_summaries[summary_slot].frame != frame)
                || (m_remote_summary_valid[summary_slot]
                    && m_remote_summaries[summary_slot].frame != frame))
            {
                fail_closed("unacknowledged-summary-ring-overwrite");
                return false;
            }
            if (m_local_summary_valid[summary_slot]
                && m_local_summaries[summary_slot].frame == frame
                && m_local_summaries[summary_slot].flags
                    == kRollbackProductionSummaryConfirmed)
            {
                fail_closed("gekko-rewound-confirmed-summary");
                return false;
            }
            m_local_summaries[summary_slot] = summary;
            m_local_summary_valid[summary_slot] = true;
            m_post_advance_state = std::move(final_state);
            m_post_advance_frame = frame;
            m_post_advance_epoch = m_manifest.epoch.generation;
            m_status.corrected_frame = frame;
            if (!event.data.adv.rolling_back
                && !event.data.adv.running_ahead)
            {
                if (!m_first_advance_frame.valid)
                    m_first_advance_frame = frame;
                if (!publish_confirmed_summary(frame)) return false;
            }
            if (!m_ledger.report().ok)
            {
                fail_closed(m_ledger.report().failure);
                return false;
            }
            return true;
        }
#endif

        void accept_peer_summary(const RollbackUdpMessage& message) noexcept
        {
            if (message.payload_bytes != sizeof(RollbackProductionFrameSummary))
            {
                fail_closed("peer-summary-size-mismatch");
                return;
            }
            RollbackProductionFrameSummary summary {};
            std::memcpy(&summary, message.payload.data(), sizeof(summary));
            if (summary.epoch != m_pair_epoch
                || summary.canonical_hash == 0
                || (summary.flags != kRollbackProductionSummaryConfirmed
                    && summary.flags != kRollbackProductionSummaryAck))
            {
                fail_closed("peer-summary-epoch-mismatch");
                return;
            }
            const RollbackSummaryFrameClass frame_class =
                m_summary_consensus.classify(summary.frame);
            if (frame_class == RollbackSummaryFrameClass::Stale)
            {
                // A newly sequenced retransmission can outlive the 128-slot
                // application ring. It is already below the contiguous pair
                // frontier: re-ACK Confirmed so the peer can retire it, and
                // ignore stale ACKs without touching the reused slot.
                if (summary.flags
                    == kRollbackProductionSummaryConfirmed)
                {
                    if (!send_summary_ack(summary))
                        fail_closed("stale-summary-ack-queue-failed");
                }
                return;
            }
            if (frame_class == RollbackSummaryFrameClass::TooFarAhead)
            {
                fail_closed("peer-summary-outside-acceptance-window");
                return;
            }
            const size_t slot = summary.frame & 127u;
            if (summary.flags == kRollbackProductionSummaryAck)
            {
                const RollbackProductionFrameSummary& local =
                    m_local_summaries[slot];
                if (local.frame != summary.frame
                    || local.epoch != summary.epoch
                    || local.flags != kRollbackProductionSummaryConfirmed
                    || local.canonical_hash != summary.canonical_hash
                    || local.input[0] != summary.input[0]
                    || local.input[1] != summary.input[1])
                {
                    fail_closed("invalid-confirmed-summary-ack");
                    return;
                }
                if (!m_summary_consensus.observe_peer_ack(summary.frame))
                {
                    fail_closed("summary-ack-window-collision");
                    return;
                }
                advance_pair_frontier();
                return;
            }

            if (m_remote_summary_valid[slot]
                && m_remote_summaries[slot].frame != summary.frame)
            {
                fail_closed("remote-summary-ring-overwrite");
                return;
            }
            m_remote_summaries[slot] = summary;
            m_remote_summary_valid[slot] = true;
            match_summary(summary.frame);
        }

        bool publish_confirmed_summary(uint32_t current_frame) noexcept
        {
            if (!m_first_advance_frame.valid) return true;
            uint32_t confirmed_frame = 0;
            if (!RollbackTryGetConfirmedFrame(
                    current_frame,
                    m_first_advance_frame.value,
                    m_config.rollback_window,
                    confirmed_frame))
                return true;

            // Gekko's health-check horizon is
            // (current - input_prediction_window) - 1. Frames newer than this
            // may still contain a predicted remote input and must not be
            // compared or used to commit presentation effects.
            uint32_t frame_to_publish = 0;
            bool publish_new = false;
            if (!m_last_summary_published.valid)
            {
                frame_to_publish = m_first_advance_frame.value;
                publish_new = !RollbackFrameIsAfter(
                    frame_to_publish, confirmed_frame);
            }
            else if (RollbackFrameIsAfter(
                         confirmed_frame,
                         m_last_summary_published.value))
            {
                frame_to_publish = m_last_summary_published.value + 1u;
                publish_new = true;
            }

            if (publish_new)
            {
                const size_t slot = frame_to_publish & 127u;
                if (!m_local_summary_valid[slot]
                    || m_local_summaries[slot].frame != frame_to_publish
                    || m_local_summaries[slot].epoch != m_pair_epoch)
                {
                    fail_closed("confirmed-frame-summary-unavailable");
                    return false;
                }
                m_local_summaries[slot].flags =
                    kRollbackProductionSummaryConfirmed;
                m_last_summary_published = frame_to_publish;
                m_summary_consensus.start(frame_to_publish);
                if (!m_network.enqueue(
                        RollbackProtocolV2PacketType::Input,
                        &m_local_summaries[slot],
                        sizeof(m_local_summaries[slot]),
                        {},
                        m_session_handshake_generation))
                {
                    fail_closed("confirmed-summary-queue-failed");
                    return false;
                }
                match_summary(frame_to_publish);
                if (m_status.state == RollbackProductionState::Fatal)
                    return false;
            }

            if (!send_oldest_unacknowledged_summary())
            {
                fail_closed("confirmed-summary-queue-failed");
                return false;
            }
            return m_status.state != RollbackProductionState::Fatal;
        }

        bool send_oldest_unacknowledged_summary() noexcept
        {
            const RollbackProductionFrameSummary* oldest = nullptr;
            for (size_t slot = 0; slot < m_local_summaries.size(); ++slot)
            {
                const RollbackProductionFrameSummary& candidate =
                    m_local_summaries[slot];
                if (!m_local_summary_valid[slot]
                    || m_summary_consensus.is_peer_acked(candidate.frame)
                    || candidate.flags
                        != kRollbackProductionSummaryConfirmed)
                {
                    continue;
                }
                if (!oldest
                    || RollbackFrameIsAfter(
                        oldest->frame, candidate.frame))
                {
                    oldest = &candidate;
                }
            }
            return !oldest || m_network.enqueue(
                RollbackProtocolV2PacketType::Input,
                oldest,
                sizeof(*oldest),
                {},
                m_session_handshake_generation);
        }

        bool send_summary_ack(
            const RollbackProductionFrameSummary& received) noexcept
        {
            RollbackProductionFrameSummary ack = received;
            ack.flags = kRollbackProductionSummaryAck;
            return m_network.enqueue(
                RollbackProtocolV2PacketType::Input,
                &ack,
                sizeof(ack),
                {},
                m_session_handshake_generation);
        }

        void match_summary(uint32_t frame) noexcept
        {
            const size_t slot = frame & 127u;
            if (!m_local_summary_valid[slot] || !m_remote_summary_valid[slot])
                return;
            const auto& local = m_local_summaries[slot];
            const auto& remote = m_remote_summaries[slot];
            if (local.frame != frame || remote.frame != frame)
                return;
            // A peer can be one or more frames ahead. Its confirmed summary
            // may arrive while our same frame is still speculative; wait until
            // both sides have crossed the Gekko confirmation horizon.
            if (local.flags != kRollbackProductionSummaryConfirmed
                || remote.flags != kRollbackProductionSummaryConfirmed)
                return;
            if (local.epoch != remote.epoch
                || local.canonical_hash != remote.canonical_hash
                || local.input[0] != remote.input[0]
                || local.input[1] != remote.input[1])
            {
                fail_closed("corrected-frame-pair-mismatch");
                return;
            }
            if (!send_summary_ack(remote))
            {
                fail_closed("confirmed-summary-ack-queue-failed");
                return;
            }
            if (!m_summary_consensus.observe_local_match(frame))
            {
                fail_closed("summary-match-window-collision");
                return;
            }
            advance_pair_frontier();
        }

        void advance_pair_frontier() noexcept
        {
            // Pair acceptance is strictly contiguous. A later matching frame
            // can never skip a dropped or divergent predecessor. Requiring
            // both the local match and the echoed ACK proves both clients
            // compared the frame before either commits presentation effects.
            for (size_t count = 0; count < 128; ++count)
            {
                uint32_t expected = 0;
                if (!m_summary_consensus.pop_contiguous(expected)) break;
                const size_t expected_slot = expected & 127u;
                if (m_local_summaries[expected_slot].frame != expected
                    || m_remote_summaries[expected_slot].frame != expected)
                {
                    fail_closed("summary-frontier-storage-mismatch");
                    break;
                }
                ++m_status.pair_accepts;
                m_status.confirmed_frame = expected;
                const auto& confirmed =
                    m_local_summaries[expected_slot];
                m_status.confirmed_canonical_hash =
                    confirmed.canonical_hash;
                const uint8_t local_slot = m_config.local_player_slot;
                const uint8_t remote_slot = static_cast<uint8_t>(1u -
                    local_slot);
                m_local_confirmed_input_hash.add_scalar(expected);
                m_local_confirmed_input_hash.add_scalar(
                    confirmed.input[local_slot]);
                m_remote_confirmed_input_hash.add_scalar(expected);
                m_remote_confirmed_input_hash.add_scalar(
                    confirmed.input[remote_slot]);
                m_status.local_input_hash =
                    m_local_confirmed_input_hash.value;
                m_status.remote_input_hash =
                    m_remote_confirmed_input_hash.value;
                ++m_status.local_input_count;
                ++m_status.remote_input_count;
                m_remote_summary_valid[expected_slot] = false;
                m_local_summary_valid[expected_slot] = false;
                if (!m_pending_effect_confirmation.valid
                    || RollbackFrameIsAfter(
                        expected,
                        m_pending_effect_confirmation.value))
                {
                    m_pending_effect_confirmation = expected;
                }
            }
        }

        static uint64_t side_effect_payload_hash(
            RollbackSideEffectType type,
            const void* payload,
            size_t bytes,
            uint32_t ordinal) noexcept
        {
            RollbackHash hash {};
            hash.add_scalar(static_cast<uint8_t>(type));
            hash.add_scalar(ordinal);
            hash.add_bytes(payload, bytes);
            return hash.value ? hash.value : 1;
        }

        bool queue_side_effect(
            RollbackSideEffectType type,
            const void* payload,
            uint16_t bytes) noexcept
        {
            if (!m_accept_effects || !m_effect_frame.valid)
                return false;
            if (m_simulation_thread_id == 0
                || GetCurrentThreadId() != m_simulation_thread_id)
            {
                fail_closed("presentation-capture-off-simulation-thread");
                return false;
            }
            const uint64_t key = side_effect_payload_hash(
                type, payload, bytes, m_effect_ordinal++);
            const bool queued = m_ledger.enqueue(
                m_effect_epoch, m_effect_frame.value,
                type, key, payload, bytes);
            update_presentation_status();
            return queued;
        }

        bool queue_stage_presentation(
            const RollbackBreakableStageSnapshot& snapshot) noexcept
        {
            for (const RollbackBreakableStageRecord& record
                 : snapshot.records)
            {
                RollbackProductionStagePresentation presentation {};
                presentation.kind = static_cast<uint8_t>(record.kind);
                presentation.id = record.id;
                presentation.scalar = record.scalar;
                presentation.endurance = record.endurance;
                presentation.fade_timer = record.fade_timer;
                presentation.fade_rate = record.fade_rate;
                const RollbackSideEffectType type =
                    record.kind == RollbackBreakableActorKind::Wall
                        ? RollbackSideEffectType::WallPresentation
                        : RollbackSideEffectType::BarrierPresentation;
                if (!queue_side_effect(
                        type, &presentation, sizeof(presentation)))
                    return false;
            }
            return true;
        }

        void update_presentation_status() noexcept
        {
            const RollbackSideEffectLedgerReport& report = m_ledger.report();
            m_status.presentation_queued = report.queued;
            m_status.presentation_duplicates_suppressed = report.duplicates;
            m_status.presentation_discarded = report.discarded;
            m_status.presentation_committed = report.committed;
            m_status.presentation_exactly_once = report.ok
                && report.committed <= report.queued;
        }

        bool set_stage_component_visibility(
            uintptr_t actor,
            uintptr_t component_offset,
            bool visible) noexcept
        {
            void* component = nullptr;
            if (!SafeReadBytes(
                    reinterpret_cast<const void*>(
                        actor + component_offset),
                    &component, sizeof(component)))
                return false;
            if (!component) return true;
            using SetVisibilityFn = void(__fastcall*)(void*, bool, bool);
            reinterpret_cast<SetVisibilityFn>(
                m_image_base
                + kRollbackProductionRvaSetSceneVisibility)(
                    component, visible, true);
            return true;
        }

        bool commit_stage_presentation(
            const RollbackProductionStagePresentation& presentation,
            RollbackSideEffectType type) noexcept
        {
            const RollbackBreakableActorKind kind =
                static_cast<RollbackBreakableActorKind>(
                    presentation.kind);
            if ((type == RollbackSideEffectType::WallPresentation
                    && kind != RollbackBreakableActorKind::Wall)
                || (type == RollbackSideEffectType::BarrierPresentation
                    && kind != RollbackBreakableActorKind::Barrier))
                return false;
            uintptr_t actor = 0;
            if (!ResolveRollbackBreakableStageActor(
                    m_manifest.epoch.stage_actor_manager,
                    kind, presentation.id, actor))
                return false;
            if (kind == RollbackBreakableActorKind::Wall)
            {
                const uintptr_t offsets[] = {
                    kRollbackStageWallIntactOpaqueOffset,
                    kRollbackStageWallIntactTranslucentOffset,
                    kRollbackStageWallBrokenOpaqueOffset,
                    kRollbackStageWallBrokenTranslucentOffset,
                    kRollbackStageWallBreakingOpaqueOffset,
                    kRollbackStageWallBreakingTranslucentOffset,
                };
                for (uintptr_t offset : offsets)
                {
                    if (!set_stage_component_visibility(
                            actor, offset, false))
                        return false;
                }
                const RollbackWallPresentationVisibility visibility =
                    ComputeRollbackWallPresentationVisibility(
                        presentation.scalar,
                        presentation.fade_timer,
                        presentation.fade_rate);
                if (!visibility.valid) return false;
                return set_stage_component_visibility(
                           actor, visibility.opaque_offset,
                           visibility.opaque_visible)
                    && set_stage_component_visibility(
                           actor, visibility.translucent_offset,
                           !visibility.opaque_visible);
            }

            if (presentation.endurance <= 0
                || presentation.scalar < 0)
                return false;
            const bool broken =
                presentation.scalar >= presentation.endurance;
            return set_stage_component_visibility(
                       actor, kRollbackStageBarrierFaceOffset, !broken)
                && set_stage_component_visibility(
                       actor, kRollbackStageBarrierBackOffset, !broken)
                && set_stage_component_visibility(
                       actor, kRollbackStageBarrierBreakingOffset, broken);
        }

        static void commit_side_effect(
            const RollbackSideEffectEvent& event,
            void* context) noexcept
        {
            auto* runtime = static_cast<RollbackProductionRuntime*>(context);
            if (!runtime) return;
            if (runtime->m_simulation_thread_id == 0
                || GetCurrentThreadId()
                    != runtime->m_simulation_thread_id)
            {
                runtime->fail_closed(
                    "presentation-commit-off-simulation-thread");
                return;
            }
            runtime->m_committing_effect = true;
            if (event.type == RollbackSideEffectType::Audio
                && event.payload_bytes
                    == sizeof(RollbackProductionAudioInvocation))
            {
                RollbackProductionAudioInvocation invocation {};
                std::memcpy(&invocation, event.payload.data(),
                            sizeof(invocation));
                const uint64_t trampoline = invocation.wrapper == 0
                    ? runtime->m_audio_cue_trampoline
                    : runtime->m_audio_slot_trampoline;
                uintptr_t p1 = 0;
                uintptr_t p2 = 0;
                const bool charas_ok = RollbackReadCharaPointers(
                    runtime->m_image_base, p1, p2);
                const uintptr_t chara = invocation.chara_slot == 0
                    ? p1 : invocation.chara_slot == 1 ? p2 : 0;
                if (trampoline && charas_ok && chara)
                {
                    reinterpret_cast<AudioFn>(trampoline)(
                        invocation.unused, static_cast<int64_t>(chara),
                        invocation.cue_id, invocation.cue_sub_id);
                }
                else
                {
                    runtime->fail_closed(
                        "confirmed-audio-target-unresolved");
                }
            }
            else if (event.type == RollbackSideEffectType::Vfx
                     && event.payload_bytes
                        == sizeof(RollbackProductionVfxInvocation))
            {
                RollbackProductionVfxInvocation invocation {};
                std::memcpy(&invocation, event.payload.data(),
                            sizeof(invocation));
                void* dispatcher = nullptr;
                const bool dispatcher_ok = SafeReadPtr(
                    reinterpret_cast<const void*>(
                        runtime->m_image_base
                        + kRollbackProductionRvaVfxDispatcher),
                    &dispatcher);
                if (runtime->m_vfx_trampoline
                    && dispatcher_ok && dispatcher)
                {
                    reinterpret_cast<VfxFn>(runtime->m_vfx_trampoline)(
                        dispatcher,
                        invocation.request);
                }
                else
                {
                    runtime->fail_closed(
                        "confirmed-vfx-target-unresolved");
                }
            }
            else if ((event.type
                        == RollbackSideEffectType::WallPresentation
                      || event.type
                        == RollbackSideEffectType::BarrierPresentation)
                     && event.payload_bytes
                        == sizeof(RollbackProductionStagePresentation))
            {
                RollbackProductionStagePresentation presentation {};
                std::memcpy(
                    &presentation, event.payload.data(),
                    sizeof(presentation));
                if (!runtime->commit_stage_presentation(
                        presentation, event.type))
                {
                    runtime->fail_closed(
                        "confirmed-stage-presentation-unresolved");
                }
            }
            else
            {
                runtime->fail_closed(
                    "confirmed-presentation-payload-invalid");
            }
            runtime->m_committing_effect = false;
        }

        static void __fastcall audio_cue_hook(
            uint64_t unused, int64_t chara, int32_t cue, uint32_t sub)
        {
            RollbackProductionRuntime* runtime =
                s_callback_runtime.load(std::memory_order_acquire);
            CallbackLease lease(runtime);
            if (!lease) return;
            RollbackProductionAudioInvocation invocation {};
            invocation.wrapper = 0;
            invocation.unused = unused;
            invocation.cue_id = cue;
            invocation.cue_sub_id = sub;
            uintptr_t p1 = 0;
            uintptr_t p2 = 0;
            if (RollbackReadCharaPointers(runtime->m_image_base, p1, p2))
            {
                invocation.chara_slot = chara == static_cast<int64_t>(p1)
                    ? 0 : chara == static_cast<int64_t>(p2) ? 1 : -1;
            }
            if (runtime->m_committing_effect
                && runtime->m_audio_cue_trampoline)
            {
                reinterpret_cast<AudioFn>(runtime->m_audio_cue_trampoline)(
                    unused, chara, cue, sub);
                return;
            }
            if (!runtime->m_hook_owns_tick)
            {
                if (runtime->m_audio_cue_trampoline)
                {
                    reinterpret_cast<AudioFn>(
                        runtime->m_audio_cue_trampoline)(
                            unused, chara, cue, sub);
                }
                return;
            }
            if (runtime->m_hook_owns_tick
                && (runtime->m_status.state
                        != RollbackProductionState::Active
                    || !runtime->m_accept_effects
                    || !runtime->m_effect_frame.valid))
                return;
            if (invocation.chara_slot < 0)
            {
                runtime->fail_closed("audio-chara-logical-slot-unresolved");
                return;
            }
            if (!runtime->queue_side_effect(
                    RollbackSideEffectType::Audio,
                    &invocation, sizeof(invocation)))
                runtime->fail_closed(runtime->m_ledger.report().failure);
        }

        static void __fastcall audio_slot_hook(
            uint64_t unused, int64_t chara, int32_t cue, uint32_t sub)
        {
            RollbackProductionRuntime* runtime =
                s_callback_runtime.load(std::memory_order_acquire);
            CallbackLease lease(runtime);
            if (!lease) return;
            RollbackProductionAudioInvocation invocation {};
            invocation.wrapper = 1;
            invocation.unused = unused;
            invocation.cue_id = cue;
            invocation.cue_sub_id = sub;
            uintptr_t p1 = 0;
            uintptr_t p2 = 0;
            if (RollbackReadCharaPointers(runtime->m_image_base, p1, p2))
            {
                invocation.chara_slot = chara == static_cast<int64_t>(p1)
                    ? 0 : chara == static_cast<int64_t>(p2) ? 1 : -1;
            }
            if (runtime->m_committing_effect
                && runtime->m_audio_slot_trampoline)
            {
                reinterpret_cast<AudioFn>(runtime->m_audio_slot_trampoline)(
                    unused, chara, cue, sub);
                return;
            }
            if (!runtime->m_hook_owns_tick)
            {
                if (runtime->m_audio_slot_trampoline)
                {
                    reinterpret_cast<AudioFn>(
                        runtime->m_audio_slot_trampoline)(
                            unused, chara, cue, sub);
                }
                return;
            }
            if (runtime->m_hook_owns_tick
                && (runtime->m_status.state
                        != RollbackProductionState::Active
                    || !runtime->m_accept_effects
                    || !runtime->m_effect_frame.valid))
                return;
            if (invocation.chara_slot < 0)
            {
                runtime->fail_closed(
                    "audio-chara-logical-slot-unresolved");
                return;
            }
            if (!runtime->queue_side_effect(
                    RollbackSideEffectType::Audio,
                    &invocation, sizeof(invocation)))
                runtime->fail_closed(runtime->m_ledger.report().failure);
        }

        static void __fastcall vfx_hook(void* dispatcher, const void* request)
        {
            RollbackProductionRuntime* runtime =
                s_callback_runtime.load(std::memory_order_acquire);
            CallbackLease lease(runtime);
            if (!lease) return;
            RollbackProductionVfxInvocation invocation {};
            const bool read = request && SafeReadBytes(
                request, invocation.request, sizeof(invocation.request));
            if (runtime->m_committing_effect
                && runtime->m_vfx_trampoline)
            {
                reinterpret_cast<VfxFn>(runtime->m_vfx_trampoline)(
                    dispatcher, request);
                return;
            }
            if (!runtime->m_hook_owns_tick)
            {
                if (runtime->m_vfx_trampoline)
                {
                    reinterpret_cast<VfxFn>(runtime->m_vfx_trampoline)(
                        dispatcher, request);
                }
                return;
            }
            if (runtime->m_hook_owns_tick
                && (runtime->m_status.state
                        != RollbackProductionState::Active
                    || !runtime->m_accept_effects
                    || !runtime->m_effect_frame.valid))
                return;
            if (!read)
            {
                runtime->fail_closed("vfx-request-read-failed");
                return;
            }
            if (!runtime->queue_side_effect(
                    RollbackSideEffectType::Vfx,
                    &invocation, sizeof(invocation)))
                runtime->fail_closed(runtime->m_ledger.report().failure);
        }

        static void __fastcall wind_hook(void* emitter)
        {
            RollbackProductionRuntime* runtime =
                s_callback_runtime.load(std::memory_order_acquire);
            CallbackLease lease(runtime);
            if (!lease) return;
            if (!runtime->m_hook_owns_tick)
            {
                if (runtime->m_wind_trampoline)
                {
                    reinterpret_cast<WindFn>(runtime->m_wind_trampoline)(
                        emitter);
                }
                return;
            }
            if (runtime->m_hook_owns_tick
                && runtime->m_status.state
                    != RollbackProductionState::Active)
                return;
            if (!CurrentRollbackResimContext().active)
            {
                reinterpret_cast<WindFn>(runtime->m_wind_trampoline)(emitter);
                return;
            }
            if (!runtime->advance_wind_emitter_without_allocation(emitter))
                runtime->fail_closed("wind-resim-progression-failed");
        }

        bool advance_wind_emitter_without_allocation(void* emitter) noexcept
        {
            if (!emitter) return false;
            __try
            {
                auto* bytes = static_cast<uint8_t*>(emitter);
                const auto* freeze = reinterpret_cast<const uint8_t*>(
                    m_image_base + kRollbackProductionRvaVmFreeze);
                if (freeze[0] != 0) return true;
                auto& active = *reinterpret_cast<int32_t*>(bytes + 0x50);
                auto& remaining = *reinterpret_cast<int32_t*>(bytes + 0x54);
                const float base_timer =
                    *reinterpret_cast<float*>(bytes + 0x58);
                auto& reload_timer = *reinterpret_cast<float*>(bytes + 0x5C);
                const float jitter = *reinterpret_cast<float*>(bytes + 0xA4);
                const float scaled_alpha =
                    *reinterpret_cast<const float*>(freeze + 0x2C);
                const float normalize = *reinterpret_cast<const float*>(
                    m_image_base + kRollbackProductionRvaRandNormalize);
                const RandFn random = reinterpret_cast<RandFn>(
                    m_image_base + kRollbackProductionRvaRandU32);
                do
                {
                    if (reload_timer <= 0.0f)
                    {
                        const uint32_t value = random();
                        reload_timer = static_cast<float>(value & 0x7FFFFFu)
                            * normalize * jitter + base_timer;
                    }
                    else
                    {
                        reload_timer -= scaled_alpha;
                    }
                    const int32_t previous = remaining;
                    if (active == 0) break;
                    remaining = previous - 1;
                    if (previous == 0) break;
                } while (true);
                active = 0;
                remaining = 0;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void update_network_status(
            const RollbackUdpWorkerStatus& network) noexcept
        {
            m_status.network_profile = static_cast<uint8_t>(
                network.network_profile);
            m_status.fault_seed = network.fault_seed;
            m_status.network_packets_sent = network.packets_sent;
            m_status.network_packets_received = network.packets_received;
            m_status.network_packets_rejected = network.packets_rejected;
            m_status.fault_packets_submitted =
                network.fault_packets_submitted;
            m_status.fault_packets_queued = network.fault_packets_queued;
            m_status.fault_packets_delivered =
                network.fault_packets_delivered;
            m_status.fault_packets_dropped = network.fault_packets_dropped;
            m_status.fault_packets_duplicated =
                network.fault_packets_duplicated;
            m_status.fault_packets_reordered =
                network.fault_packets_reordered;
            m_status.fault_packets_corrupted =
                network.fault_packets_corrupted;
            m_status.fault_packets_spiked = network.fault_packets_spiked;
            m_status.fault_packets_burst_dropped =
                network.fault_packets_burst_dropped;
            m_status.fault_queue_overflows =
                network.fault_queue_overflows;
        }

        void fail_before_activation(const char* failure) noexcept
        {
            if (m_hook_owns_tick)
            {
                fail_closed(failure);
                return;
            }
            // Pre-activation failures must not leave a UDP worker, Gekko
            // session, or a subset of native detours alive. Preserve the
            // diagnostic status while releasing all runtime ownership.
            m_accept_effects = false;
            stop_callback_admission();
            unhook(m_tick_detour);
            unhook(m_vfx_detour);
            unhook(m_audio_cue_detour);
            unhook(m_audio_slot_detour);
            unhook(m_wind_detour);
            m_network.stop();
            m_network_started = false;
            m_gekko.shutdown();
            m_gekko_session_started = false;
            m_hook_owns_tick = false;
            m_store.clear();
            m_ledger.clear();
            m_status.state = RollbackProductionState::Fatal;
            m_status.failure = failure ? failure : "pre-activation-failure";
            m_status.tick_hook_installed = false;
            m_status.presentation_hooks_installed = false;
            m_status.lobby_return_requested = false;
            m_pending_pre_activation_failure = nullptr;
        }

        void fail_closed(const char* failure) noexcept
        {
            if (!m_hook_owns_tick)
            {
                if (!m_pending_pre_activation_failure)
                {
                    m_pending_pre_activation_failure = failure
                        ? failure : "pre-activation-callback-failure";
                }
                return;
            }
            if (m_status.state == RollbackProductionState::Fatal) return;
            m_status.state = RollbackProductionState::Fatal;
            m_status.failure = failure ? failure : "production-failure";
            m_status.lobby_return_requested = m_hook_owns_tick;
            m_accept_effects = false;
            const char* reason = m_status.failure;
            const RollbackUdpWorkerStatus network = m_network.status();
            if (network.peer_ready
                && network.failure == RollbackUdpWorkerFailure::None
                && network.handshake_generation
                    == m_session_handshake_generation)
            {
                (void)m_network.enqueue(
                    RollbackProtocolV2PacketType::Desync,
                    reason,
                    static_cast<uint16_t>((std::min)(
                        std::strlen(reason),
                        kRollbackProtocolV2MaxPayloadBytes)),
                    {},
                    m_session_handshake_generation);
            }
        }

        RollbackProductionConfig m_config {};
        RollbackProductionStatus m_status {};
        uintptr_t m_image_base {0};
        RollbackSnapshotManifest m_manifest {};
        RollbackUdpNetworkWorker m_network {};
        RollbackSnapshotStore<RollbackStepState> m_store {};
        RollbackSideEffectLedger<> m_ledger {};
        RollbackHistoricalCameraArgs<128> m_camera_history {};
        RollbackFrameStamp m_current_frame {};
        RollbackFrameStamp m_effect_frame {};
        RollbackFrameStamp m_first_advance_frame {};
        RollbackFrameStamp m_last_summary_published {};
        RollbackSummaryConsensusWindow<128> m_summary_consensus {};
        RollbackHash m_local_confirmed_input_hash {};
        RollbackHash m_remote_confirmed_input_hash {};
        RollbackFrameStamp m_pending_effect_confirmation {};
        RollbackStepState m_post_advance_state {};
        RollbackFrameStamp m_post_advance_frame {};
        uint64_t m_post_advance_epoch {0};
        RollbackFrameStamp m_gekko_frame_high_water {};
        uint64_t m_gekko_update_calls {0};
        uint64_t m_effect_epoch {0};
        uint64_t m_pair_epoch {0};
        uint32_t m_effect_ordinal {0};
        DWORD m_simulation_thread_id {0};
        bool m_accept_effects {false};
        bool m_committing_effect {false};
        bool m_hook_owns_tick {false};
        bool m_network_started {false};
        bool m_gekko_session_started {false};
        uint64_t m_session_handshake_generation {0};
        const char* m_pending_pre_activation_failure {nullptr};
        RollbackLaunchBarrierMessage m_local_launch_barrier {};
        RollbackLaunchBarrierMessage m_remote_launch_barrier {};
        bool m_local_launch_barrier_valid {false};
        bool m_remote_launch_barrier_valid {false};
        uint64_t m_launch_barrier_generation {0};
        uint64_t m_launch_barrier_service_ticks {0};
        std::atomic<bool> m_callbacks_accepting {false};
        std::atomic<uint32_t> m_inflight_callbacks {0};

        std::array<RollbackProductionFrameSummary, 128> m_local_summaries {};
        std::array<RollbackProductionFrameSummary, 128> m_remote_summaries {};
        std::array<bool, 128> m_local_summary_valid {};
        std::array<bool, 128> m_remote_summary_valid {};

        std::unique_ptr<PLH::x64Detour> m_tick_detour;
        std::unique_ptr<PLH::x64Detour> m_audio_cue_detour;
        std::unique_ptr<PLH::x64Detour> m_audio_slot_detour;
        std::unique_ptr<PLH::x64Detour> m_vfx_detour;
        std::unique_ptr<PLH::x64Detour> m_wind_detour;
        uint64_t m_tick_trampoline {0};
        uint64_t m_audio_cue_trampoline {0};
        uint64_t m_audio_slot_trampoline {0};
        uint64_t m_vfx_trampoline {0};
        uint64_t m_wind_trampoline {0};

        RollbackGekkoRuntimeCore m_gekko {};
        inline static std::atomic<RollbackProductionRuntime*>
            s_callback_runtime {nullptr};
    };
}
