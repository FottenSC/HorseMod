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

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if HORSE_ENABLE_GEKKONET
#include <gekkonet.h>
#endif

namespace Horse
{
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

        bool valid() const noexcept
        {
            return local_player_slot < 2 && remote_peer != 0
                && rollback_window != 0 && rollback_window <= 60
                && input_delay != 0 && input_delay <= rollback_window
                && state_size != 0;
        }
    };

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
            gekko.limited_saving = false;
            gekko.desync_detection = true;
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
            AdapterScope scope(*this);
            gekko_network_poll(m_session);
            return !m_fatal && process_session_events() && !m_fatal;
        }

        bool update(uint32_t local_input, const void* auxiliary) noexcept
        {
            // Gekko emits its initial baseline Save/Advance batch from the
            // first update. Requiring GekkoSessionStarted here deadlocks both
            // peers because that session event is itself produced by this
            // bootstrap traffic.
            if (!m_session || m_fatal) return false;
            AdapterScope scope(*this);
            gekko_add_local_input(
                m_session, m_config.local_player_slot, &local_input);
            int event_count = 0;
            GekkoGameEvent** events =
                gekko_update_session(m_session, &event_count);
            ++m_update_calls;
            if (m_fatal || !process_session_events()) return false;
            for (int index = 0; index < event_count; ++index)
            {
                GekkoGameEvent* event = events[index];
                if (!event || !m_callbacks.game_event(
                        m_callbacks.context, *event, auxiliary))
                    return false;
                if (m_fatal) return false;
            }
            const bool ok = event_count != 0 || !m_callbacks.idle_update
                || m_callbacks.idle_update(m_callbacks.context);
            return ok && !m_fatal;
        }

        // Process corrections made ready by NetworkPoll without submitting a
        // new local frame. Replay-fork uses this only after its fixed terminal
        // frame, so delayed terminal input can still emit Load/Advance events
        // before the final consensus proof is accepted.
        bool drain(const void* auxiliary) noexcept
        {
            if (!m_session || m_fatal) return false;
            AdapterScope scope(*this);
            int event_count = 0;
            GekkoGameEvent** events =
                gekko_update_session(m_session, &event_count);
            if (m_fatal || !process_session_events()) return false;
            for (int index = 0; index < event_count; ++index)
            {
                GekkoGameEvent* event = events[index];
                if (!event || !m_callbacks.game_event(
                        m_callbacks.context, *event, auxiliary))
                    return false;
                if (m_fatal) return false;
            }
            return !m_fatal;
        }

        bool created() const noexcept { return m_session != nullptr; }
        bool session_started() const noexcept { return m_session_started; }
        uint64_t update_calls() const noexcept { return m_update_calls; }
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
                    > kRollbackProtocolV2MaxPayloadBytes
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
                RollbackGekkoDatagram packet {};
                const RollbackGekkoReceiveStatus status =
                    core->m_callbacks.receive(
                        core->m_callbacks.context, packet);
                if (status == RollbackGekkoReceiveStatus::Empty) break;
                if (status == RollbackGekkoReceiveStatus::Fatal)
                {
                    core->fail("gekko-receive-failed");
                    break;
                }
                if (packet.remote_peer != core->m_config.remote_peer
                    || packet.bytes == 0
                    || packet.bytes > packet.payload.size())
                {
                    core->fail("gekko-receive-packet-invalid");
                    break;
                }

                auto* result = static_cast<GekkoNetResult*>(
                    std::malloc(sizeof(GekkoNetResult)));
                auto* address = static_cast<uint8_t*>(std::malloc(1));
                void* payload = std::malloc(packet.bytes);
                if (!result || !address || !payload)
                {
                    std::free(result);
                    std::free(address);
                    std::free(payload);
                    core->fail("gekko-receive-allocation-failed");
                    break;
                }
                *address = packet.remote_peer;
                std::memcpy(payload, packet.payload.data(), packet.bytes);
                result->addr.data = address;
                result->addr.size = 1;
                result->data_len = packet.bytes;
                result->data = payload;
                core->m_receive_results[static_cast<size_t>(*length)] =
                    result;
                ++*length;
            }
            return core->m_receive_results.data();
        }

        static void adapter_free(void* memory) noexcept
        {
            std::free(memory);
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
        uint64_t m_desync_events {0};
        int m_last_desync_frame {-1};
        uint32_t m_last_desync_local_checksum {0};
        uint32_t m_last_desync_remote_checksum {0};
        uint64_t m_disconnect_events {0};
        std::array<GekkoNetResult*, 256> m_receive_results {};
        inline static thread_local RollbackGekkoRuntimeCore*
            s_adapter_core {nullptr};
    };
#else
    class RollbackGekkoRuntimeCore
    {
    public:
        void shutdown() noexcept {}
        bool created() const noexcept { return false; }
        bool session_started() const noexcept { return false; }
        uint64_t update_calls() const noexcept { return 0; }
    };
#endif
}
