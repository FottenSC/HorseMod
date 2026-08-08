// ============================================================================
// Horse::RollbackGekkoRuntimeCore
//
// Shared, transport-agnostic owner of the GekkoNet session and event pump.
// Production and replay-fork drivers provide authenticated datagram I/O and
// simulation callbacks; neither may bypass this Save/Load/Advance dispatcher.
// ============================================================================

#pragma once

#ifndef HORSE_ENABLE_GEKKONET
#define HORSE_ENABLE_GEKKONET 0
#endif

#include "RollbackGekkoSessionStart.hpp"
#include "RollbackProtocolV2.hpp"
#include "RollbackRuntimePolicy.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#if HORSE_ENABLE_GEKKONET
#include <gekkonet.h>
#endif

namespace Horse
{
    // One adapter receive call owns this many fixed result slots. Keep
    // downstream batch-deferred presentation storage large enough to retain
    // a full ingress batch without committing mid-Gekko-update.
    static constexpr size_t kRollbackGekkoReceiveBatchCapacity = 256;

    enum class RollbackGekkoReceiveStatus : uint8_t
    {
        Empty,
        Packet,
        Fatal,
    };

    struct RollbackGekkoDatagram
    {
        uint8_t remote_peer {0};
        uint16_t bytes {0};
        std::array<uint8_t, kRollbackProtocolV2MaxPayloadBytes> payload {};
    };

#if HORSE_ENABLE_GEKKONET
    struct RollbackGekkoRuntimeCallbacks
    {
        void* context {nullptr};
        bool (*send)(void*, uint8_t, const void*, uint16_t) noexcept {nullptr};
        RollbackGekkoReceiveStatus (*receive)(
            void*, RollbackGekkoDatagram&) noexcept {nullptr};
        bool (*game_event)(void*, GekkoGameEvent&, const void*) noexcept {
            nullptr};
        bool (*idle_update)(void*) noexcept {nullptr};
        void (*failure)(void*, const char*) noexcept {nullptr};

        bool valid() const noexcept
        {
            return context && send && receive && game_event && failure;
        }
    };

    struct RollbackGekkoRuntimeConfig
    {
        uint8_t local_player_slot {0};
        uint8_t remote_peer {0};
        uint16_t rollback_window {60};
        uint16_t input_delay {1};
        uint32_t state_size {0};
        RollbackSavePolicy save_policy {
            RollbackSavePolicy::ConfirmedSpeculative};
        uint8_t forced_rollback_depth {0};

        bool valid() const noexcept
        {
            return local_player_slot < 2 && remote_peer != 0
                && rollback_window != 0 && rollback_window <= 60
                && input_delay <= rollback_window
                && RollbackSavePolicyValid(save_policy)
                && forced_rollback_depth <= rollback_window
                && (forced_rollback_depth == 0
                    || save_policy == RollbackSavePolicy::EveryAdvance)
                && state_size != 0;
        }
    };

    static inline uint32_t RollbackGekkoNextInputFrameAfterEvent(
        uint32_t current, const GekkoGameEvent& event) noexcept
    {
        // SyncSystem's real frame advances only for the ordinary Advance.
        // Historical rollback and provisional runahead callbacks must not
        // move the deterministic input-submission clock.
        if (event.type == GekkoAdvanceEvent
            && !event.data.adv.rolling_back
            && !event.data.adv.running_ahead
            && event.data.adv.frame >= 0)
        {
            return static_cast<uint32_t>(event.data.adv.frame) + 1u;
        }
        return current;
    }

    constexpr bool RollbackGekkoPreGameplayPollRequired(
        bool session_started,
        bool replay_input_enabled,
        bool bilateral_prefix_ready) noexcept
    {
        return !session_started
            || (replay_input_enabled && !bilateral_prefix_ready);
    }

    constexpr bool RollbackGekkoPreGameplayFrameZeroBlocked(
        bool replay_input_enabled,
        bool bilateral_prefix_ready) noexcept
    {
        return replay_input_enabled && !bilateral_prefix_ready;
    }

    class RollbackGekkoRuntimeCore
    {
    public:
        bool start(
            const RollbackGekkoRuntimeConfig& config,
            const RollbackGekkoRuntimeCallbacks& callbacks) noexcept
        {
            shutdown();
            if (!config.valid() || !callbacks.valid()) return false;
            m_config = config;
            m_callbacks = callbacks;
            if (!gekko_create(&m_session, GekkoGameSession) || !m_session)
                return false;

            GekkoConfig gekko {};
            gekko.num_players = 2;
            gekko.max_spectators = 0;
            gekko.input_prediction_window = static_cast<unsigned char>(
                config.rollback_window);
            gekko.input_size = sizeof(uint32_t);
            gekko.state_size = config.state_size;
            gekko.limited_saving = config.save_policy
                == RollbackSavePolicy::ConfirmedSpeculative;
            gekko.save_policy = config.save_policy
                    == RollbackSavePolicy::ConfirmedSpeculative
                ? GekkoSaveConfirmedSpeculative
                : GekkoSaveEveryAdvance;
            // Horse compares authenticated 64-bit canonical summaries at the
            // contiguous confirmed-frame frontier. Gekko's earlier 32-bit
            // checksum abort races that stronger proof and hides component
            // diagnostics, so keep one desync authority.
            gekko.desync_detection = false;
            // Gekko's network clock advances only when this game-thread core
            // is polled. Horse's authenticated transport worker continues to
            // observe traffic during a game-thread stall, so it is the only
            // sound disconnect authority for production rollback sessions.
            gekko.external_disconnect_detection = true;
            gekko.check_distance = 1;
            {
                AdapterScope scope(*this);
                if (!StartRollbackGekkoSessionWithAdapter(
                        m_session, gekko, adapter()))
                {
                    shutdown();
                    return false;
                }
            }

            GekkoNetAddress remote_address {};
            m_remote_actor_address = config.remote_peer;
            remote_address.data = &m_remote_actor_address;
            remote_address.size = 1;
            for (uint8_t slot = 0; slot < 2; ++slot)
            {
                const bool local = slot == config.local_player_slot;
                const int handle = gekko_add_actor(
                    m_session,
                    local ? GekkoLocalPlayer : GekkoRemotePlayer,
                    local ? nullptr : &remote_address);
                if (handle != slot)
                {
                    shutdown();
                    return false;
                }
            }
            gekko_set_local_delay(
                m_session, config.local_player_slot,
                static_cast<unsigned char>(config.input_delay));
            if (!gekko_set_forced_rollback_depth(
                    m_session, config.forced_rollback_depth))
            {
                shutdown();
                return false;
            }
            m_pre_activity = true;
            return true;
        }

        void shutdown() noexcept
        {
            if (m_session) (void)gekko_destroy(&m_session);
            m_session = nullptr;
            m_session_started = false;
            m_fatal = false;
            m_failure_reason = nullptr;
            m_update_calls = 0;
            m_correction_flush_calls = 0;
            m_forced_rollback_eligible_updates = 0;
            m_forced_rollback_completed_updates = 0;
            m_delay_prefix_inputs = 0;
            m_delay_prefix_primed = false;
            m_pre_activity = false;
            m_next_input_frame = 0;
            m_desync_events = 0;
            m_last_desync_frame = -1;
            m_last_desync_local_checksum = 0;
            m_last_desync_remote_checksum = 0;
            m_disconnect_events = 0;
            m_callbacks = {};
            m_config = {};
            m_receive_results.fill(nullptr);
        }

        bool poll() noexcept
        {
            if (!m_session || m_fatal) return false;
            m_pre_activity = false;
            AdapterScope scope(*this);
            gekko_network_poll(m_session);
            return !m_fatal && process_session_events() && !m_fatal;
        }

        // Replay inputs are authored for consumed logical frames. Seed the
        // local delay slots before the first Gekko update so input delay only
        // changes submission timing; this call performs no poll or simulation.
        bool prime_local_delay_prefix(
            const uint32_t* inputs, uint16_t count) noexcept
        {
            if (!m_session || m_fatal || (!inputs && count != 0)
                || !m_pre_activity || m_delay_prefix_primed
                || count != m_config.input_delay || count > UINT8_MAX)
                return false;
            if (count != 0 && !gekko_prime_local_delay_prefix(
                    m_session, m_config.local_player_slot,
                    const_cast<uint32_t*>(inputs),
                    static_cast<unsigned char>(count)))
                return false;
            m_delay_prefix_inputs = count;
            m_delay_prefix_primed = true;
            return true;
        }

        bool delay_prefix_ready() const noexcept
        {
            return m_session && !m_fatal && m_delay_prefix_primed
                && gekko_delay_prefix_ready(
                    m_session, static_cast<unsigned char>(
                        m_delay_prefix_inputs));
        }

        bool update(uint32_t local_input, const void* auxiliary) noexcept
        {
            // Gekko emits its initial baseline Save/Advance batch from the
            // first update. Requiring GekkoSessionStarted here deadlocks both
            // peers because that session event is itself produced by this
            // bootstrap traffic.
            if (!m_session || m_fatal) return false;
            m_pre_activity = false;
            const bool forced_rollback_expected =
                m_config.forced_rollback_depth != 0
                && m_session_started
                && m_next_input_frame > m_config.forced_rollback_depth;
            const uint32_t forced_load_frame = forced_rollback_expected
                ? m_next_input_frame - m_config.forced_rollback_depth - 1u
                : 0u;
            if (forced_rollback_expected)
                ++m_forced_rollback_eligible_updates;
            AdapterScope scope(*this);
            gekko_add_local_input(
                m_session, m_config.local_player_slot, &local_input);
            int event_count = 0;
            GekkoGameEvent** events =
                gekko_update_session(m_session, &event_count);
            ++m_update_calls;
            if (m_fatal || !process_session_events()) return false;
            if (forced_rollback_expected
                && !validate_forced_rollback_batch(
                    events, event_count, forced_load_frame,
                    m_config.forced_rollback_depth))
            {
                fail("gekko-forced-rollback-depth-invariant-failed");
                return false;
            }
            if (forced_rollback_expected)
                ++m_forced_rollback_completed_updates;
            for (int index = 0; index < event_count; ++index)
            {
                GekkoGameEvent* event = events[index];
                if (!event || !m_callbacks.game_event(
                        m_callbacks.context, *event, auxiliary))
                    return false;
                if (m_fatal) return false;
                observe_next_input_frame(*event);
            }
            const bool ok = event_count != 0 || !m_callbacks.idle_update
                || m_callbacks.idle_update(m_callbacks.context);
            return ok && !m_fatal;
        }

        // Poll, restore, and resimulate pending corrections without emitting
        // an ordinary Advance. Terminal handoff and peer-lead pacing share
        // this path so neither can author a hidden gameplay frame.
        bool flush_corrections(const void* auxiliary) noexcept
        {
            if (!m_session || m_fatal) return false;
            m_pre_activity = false;
            AdapterScope scope(*this);
            int event_count = 0;
            GekkoGameEvent** events =
                gekko_flush_corrections(m_session, &event_count);
            ++m_correction_flush_calls;
            if (m_fatal || !process_session_events()) return false;
            for (int index = 0; index < event_count; ++index)
            {
                GekkoGameEvent* event = events[index];
                if (event && event->type == GekkoAdvanceEvent
                    && !event->data.adv.rolling_back
                    && !event->data.adv.running_ahead)
                {
                    fail("gekko-correction-flush-emitted-ordinary-advance");
                    return false;
                }
                if (!event || !m_callbacks.game_event(
                        m_callbacks.context, *event, auxiliary))
                    return false;
                if (m_fatal) return false;
                observe_next_input_frame(*event);
            }
            const bool ok = event_count != 0 || !m_callbacks.idle_update
                || m_callbacks.idle_update(m_callbacks.context);
            return ok && !m_fatal;
        }

        bool flush_terminal_corrections(const void* auxiliary) noexcept
        {
            return flush_corrections(auxiliary);
        }

        bool created() const noexcept { return m_session != nullptr; }
        bool session_started() const noexcept { return m_session_started; }
        uint64_t update_calls() const noexcept { return m_update_calls; }
        uint64_t correction_flush_calls() const noexcept
        {
            return m_correction_flush_calls;
        }
        uint64_t forced_rollback_eligible_updates() const noexcept
        {
            return m_forced_rollback_eligible_updates;
        }
        uint64_t forced_rollback_completed_updates() const noexcept
        {
            return m_forced_rollback_completed_updates;
        }
        uint16_t delay_prefix_inputs() const noexcept
        {
            return m_delay_prefix_inputs;
        }
        uint32_t next_input_frame() const noexcept
        {
            return m_next_input_frame;
        }
        float frames_ahead() const noexcept
        {
            return m_session && !m_fatal
                ? gekko_frames_ahead(m_session) : 0.0f;
        }
        int32_t confirmed_input_frame() const noexcept
        {
            return m_session && !m_fatal
                ? gekko_confirmed_frame(m_session) : -1;
        }
        uint64_t desync_events() const noexcept { return m_desync_events; }
        int last_desync_frame() const noexcept { return m_last_desync_frame; }
        uint32_t last_desync_local_checksum() const noexcept
        {
            return m_last_desync_local_checksum;
        }
        uint32_t last_desync_remote_checksum() const noexcept
        {
            return m_last_desync_remote_checksum;
        }
        uint64_t disconnect_events() const noexcept
        {
            return m_disconnect_events;
        }
        bool fatal() const noexcept { return m_fatal; }
        const char* failure_reason() const noexcept
        {
            return m_failure_reason ? m_failure_reason : "";
        }

    private:
        static bool validate_forced_rollback_batch(
            GekkoGameEvent* const* events, int count,
            uint32_t load_frame, uint8_t depth) noexcept
        {
            if (!events || count <= 0 || depth == 0) return false;
            int load_index = -1;
            for (int index = 0; index < count; ++index)
            {
                const GekkoGameEvent* event = events[index];
                if (event && event->type == GekkoLoadEvent
                    && event->data.load.frame
                        == static_cast<int32_t>(load_frame))
                {
                    load_index = index;
                    break;
                }
            }
            if (load_index < 0) return false;
            uint32_t expected_frame = load_frame + 1u;
            uint32_t advances = 0;
            for (int index = load_index + 1;
                 index < count && advances < depth; ++index)
            {
                const GekkoGameEvent* event = events[index];
                if (!event || event->type != GekkoAdvanceEvent)
                    continue;
                if (!event->data.adv.rolling_back
                    || event->data.adv.running_ahead
                    || event->data.adv.frame
                        != static_cast<int32_t>(expected_frame))
                    return false;
                ++advances;
                ++expected_frame;
            }
            return advances == depth;
        }

        void observe_next_input_frame(const GekkoGameEvent& event) noexcept
        {
            m_next_input_frame = RollbackGekkoNextInputFrameAfterEvent(
                m_next_input_frame, event);
        }

        class AdapterScope
        {
        public:
            explicit AdapterScope(RollbackGekkoRuntimeCore& core) noexcept
                : m_previous(s_adapter_core)
            {
                s_adapter_core = &core;
            }
            ~AdapterScope() noexcept { s_adapter_core = m_previous; }
        private:
            RollbackGekkoRuntimeCore* m_previous {nullptr};
        };

        void fail(const char* reason) noexcept
        {
            if (m_fatal) return;
            m_fatal = true;
            m_failure_reason = reason ? reason : "gekko-runtime-failed";
            if (m_callbacks.failure)
                m_callbacks.failure(m_callbacks.context, m_failure_reason);
        }

        bool process_session_events() noexcept
        {
            int count = 0;
            GekkoSessionEvent** events =
                gekko_session_events(m_session, &count);
            for (int index = 0; index < count; ++index)
            {
                GekkoSessionEvent* event = events[index];
                if (!event) continue;
                if (event->type == GekkoSessionStarted)
                    m_session_started = true;
                else if (event->type == GekkoDesyncDetected)
                {
                    ++m_desync_events;
                    m_last_desync_frame = event->data.desynced.frame;
                    m_last_desync_local_checksum =
                        event->data.desynced.local_checksum;
                    m_last_desync_remote_checksum =
                        event->data.desynced.remote_checksum;
                    fail("gekko-desync-detected");
                    return false;
                }
                else if (event->type == GekkoPlayerDisconnected)
                {
                    ++m_disconnect_events;
                    fail("gekko-peer-disconnected");
                    return false;
                }
            }
            return true;
        }

        static void adapter_send(
            GekkoNetAddress* address,
            const char* data,
            int length) noexcept
        {
            RollbackGekkoRuntimeCore* core = s_adapter_core;
            if (!core || !address || !address->data || address->size != 1
                || !data || length <= 0
                || static_cast<size_t>(length)
                    > kRollbackGekkoRoundMaxPayloadBytes
                || *static_cast<uint8_t*>(address->data)
                    != core->m_config.remote_peer
                || !core->m_callbacks.send(
                    core->m_callbacks.context,
                    core->m_config.remote_peer,
                    data,
                    static_cast<uint16_t>(length)))
            {
                if (core) core->fail("gekko-send-queue-failed");
            }
        }

        static GekkoNetResult** adapter_receive(int* length) noexcept
        {
            RollbackGekkoRuntimeCore* core = s_adapter_core;
            if (!length) return nullptr;
            *length = 0;
            if (!core) return nullptr;
            while (*length < static_cast<int>(core->m_receive_results.size()))
            {
                ReceiveSlot& slot = core->m_receive_slots[
                    static_cast<size_t>(*length)];
                slot.packet = {};
                const RollbackGekkoReceiveStatus status =
                    core->m_callbacks.receive(
                        core->m_callbacks.context, slot.packet);
                if (status == RollbackGekkoReceiveStatus::Empty) break;
                if (status == RollbackGekkoReceiveStatus::Fatal)
                {
                    core->fail("gekko-receive-failed");
                    break;
                }
                if (slot.packet.remote_peer != core->m_config.remote_peer
                    || slot.packet.bytes == 0
                    || slot.packet.bytes > slot.packet.payload.size())
                {
                    core->fail("gekko-receive-packet-invalid");
                    break;
                }

                slot.address = slot.packet.remote_peer;
                slot.result.addr.data = &slot.address;
                slot.result.addr.size = 1;
                slot.result.data_len = slot.packet.bytes;
                slot.result.data = slot.packet.payload.data();
                core->m_receive_results[static_cast<size_t>(*length)] =
                    &slot.result;
                ++*length;
            }
            return core->m_receive_results.data();
        }

        static void adapter_free(void* memory) noexcept
        {
            // Every pointer returned by adapter_receive belongs to the fixed
            // receive pool and remains valid for the enclosing Gekko call.
            (void)memory;
        }

        static GekkoNetAdapter* adapter() noexcept
        {
            static GekkoNetAdapter value {
                &adapter_send,
                &adapter_receive,
                &adapter_free,
            };
            return &value;
        }

        RollbackGekkoRuntimeConfig m_config {};
        RollbackGekkoRuntimeCallbacks m_callbacks {};
        GekkoSession* m_session {nullptr};
        uint8_t m_remote_actor_address {0};
        bool m_session_started {false};
        bool m_fatal {false};
        const char* m_failure_reason {nullptr};
        uint64_t m_update_calls {0};
        uint64_t m_correction_flush_calls {0};
        uint64_t m_forced_rollback_eligible_updates {0};
        uint64_t m_forced_rollback_completed_updates {0};
        uint16_t m_delay_prefix_inputs {0};
        bool m_delay_prefix_primed {false};
        bool m_pre_activity {false};
        uint32_t m_next_input_frame {0};
        uint64_t m_desync_events {0};
        int m_last_desync_frame {-1};
        uint32_t m_last_desync_local_checksum {0};
        uint32_t m_last_desync_remote_checksum {0};
        uint64_t m_disconnect_events {0};
        struct ReceiveSlot
        {
            GekkoNetResult result {};
            uint8_t address {0};
            RollbackGekkoDatagram packet {};
        };
        std::array<ReceiveSlot, kRollbackGekkoReceiveBatchCapacity>
            m_receive_slots {};
        std::array<GekkoNetResult*, kRollbackGekkoReceiveBatchCapacity>
            m_receive_results {};
        inline static thread_local RollbackGekkoRuntimeCore*
            s_adapter_core {nullptr};
    };
#else
    struct GekkoGameEvent
    {
    };

    struct RollbackGekkoRuntimeCallbacks
    {
        void* context {nullptr};
        bool (*send)(void*, uint8_t, const void*, uint16_t) noexcept {nullptr};
        RollbackGekkoReceiveStatus (*receive)(
            void*, RollbackGekkoDatagram&) noexcept {nullptr};
        bool (*game_event)(void*, GekkoGameEvent&, const void*) noexcept {
            nullptr};
        bool (*idle_update)(void*) noexcept {nullptr};
        void (*failure)(void*, const char*) noexcept {nullptr};

        bool valid() const noexcept { return false; }
    };

    struct RollbackGekkoRuntimeConfig
    {
        uint8_t local_player_slot {0};
        uint8_t remote_peer {0};
        uint16_t rollback_window {60};
        uint16_t input_delay {1};
        uint32_t state_size {0};
        RollbackSavePolicy save_policy {
            RollbackSavePolicy::ConfirmedSpeculative};
        uint8_t forced_rollback_depth {0};

        bool valid() const noexcept { return false; }
    };

    constexpr bool RollbackGekkoPreGameplayPollRequired(
        bool session_started,
        bool replay_input_enabled,
        bool bilateral_prefix_ready) noexcept
    {
        return !session_started
            || (replay_input_enabled && !bilateral_prefix_ready);
    }

    constexpr bool RollbackGekkoPreGameplayFrameZeroBlocked(
        bool replay_input_enabled,
        bool bilateral_prefix_ready) noexcept
    {
        return replay_input_enabled && !bilateral_prefix_ready;
    }

    class RollbackGekkoRuntimeCore
    {
    public:
        bool start(
            const RollbackGekkoRuntimeConfig&,
            const RollbackGekkoRuntimeCallbacks&) noexcept
        {
            return false;
        }
        void shutdown() noexcept {}
        bool poll() noexcept { return false; }
        bool update(uint32_t, const void*) noexcept { return false; }
        bool flush_corrections(const void*) noexcept { return false; }
        bool flush_terminal_corrections(const void*) noexcept { return false; }
        bool prime_local_delay_prefix(const uint32_t*, uint16_t) noexcept
        {
            return false;
        }
        bool delay_prefix_ready() const noexcept { return false; }
        bool created() const noexcept { return false; }
        bool session_started() const noexcept { return false; }
        uint64_t update_calls() const noexcept { return 0; }
        uint64_t correction_flush_calls() const noexcept { return 0; }
        uint64_t forced_rollback_eligible_updates() const noexcept
        {
            return 0;
        }
        uint64_t forced_rollback_completed_updates() const noexcept
        {
            return 0;
        }
        uint16_t delay_prefix_inputs() const noexcept { return 0; }
        uint32_t next_input_frame() const noexcept { return 0; }
        float frames_ahead() const noexcept { return 0.0f; }
        int32_t confirmed_input_frame() const noexcept { return -1; }
        uint64_t desync_events() const noexcept { return 0; }
        int last_desync_frame() const noexcept { return -1; }
        uint32_t last_desync_local_checksum() const noexcept { return 0; }
        uint32_t last_desync_remote_checksum() const noexcept { return 0; }
        uint64_t disconnect_events() const noexcept { return 0; }
        bool fatal() const noexcept { return true; }
        const char* failure_reason() const noexcept
        {
            return "gekkonet-disabled";
        }
    };
#endif
}
