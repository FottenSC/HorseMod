#include "../HorseMod/horselib/RollbackSteamP2PTransport.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    struct FakeSteamPacket
    {
        uint64_t sender {0};
        uint64_t destination {0};
        int send_type {0};
        int channel {0};
        std::vector<uint8_t> bytes {};
    };

    struct FakeSteamBus
    {
        std::mutex mutex {};
        std::deque<FakeSteamPacket> packets {};
    };

    class FakeSteamApi final : public Horse::IRollbackSteamLegacyApi
    {
    public:
        FakeSteamApi(
            FakeSteamBus& bus,
            uint64_t local_steam_id,
            bool using_relay) noexcept
            : m_bus(bus),
              m_local_steam_id(local_steam_id),
              m_using_relay(using_relay)
        {
        }

        bool initialize() noexcept override
        {
            ++m_initialize_calls;
            if (m_initialize_failures != 0)
            {
                --m_initialize_failures;
                return false;
            }
            m_initialized = true;
            return true;
        }

        bool available() const noexcept override
        {
            return m_initialized;
        }

        bool send(
            uint64_t remote_steam_id,
            const void* data,
            uint32_t bytes,
            int send_type,
            int channel) noexcept override
        {
            if (!available() || remote_steam_id == 0 || !data || bytes == 0)
                return false;
            try
            {
                FakeSteamPacket packet {};
                packet.sender = m_local_steam_id;
                packet.destination = remote_steam_id;
                packet.send_type = send_type;
                packet.channel = channel;
                const auto* begin = static_cast<const uint8_t*>(data);
                packet.bytes.assign(begin, begin + bytes);
                std::lock_guard<std::mutex> lock(m_bus.mutex);
                m_bus.packets.push_back(std::move(packet));
                if (send_type == Horse::kRollbackSteamP2PSendReliable)
                {
                    ++m_reliable_sends;
                    if (bytes >= sizeof(Horse::RollbackProtocolV2Header))
                    {
                        Horse::RollbackProtocolV2Header header {};
                        std::memcpy(&header, data, sizeof(header));
                        if (header.magic == Horse::kRollbackProtocolV2Magic
                            && (header.packet_type
                                    == Horse::RollbackProtocolV2PacketType::Hello
                                || header.packet_type
                                    == Horse::RollbackProtocolV2PacketType
                                        ::HelloAck))
                        {
                            ++m_reliable_protocol_handshakes;
                        }
                    }
                }
                else
                    ++m_unreliable_sends;
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool packet_available(
            uint32_t& bytes,
            int channel) noexcept override
        {
            bytes = 0;
            std::lock_guard<std::mutex> lock(m_bus.mutex);
            const auto found = find_packet(channel);
            if (found == m_bus.packets.end()) return false;
            bytes = static_cast<uint32_t>(found->bytes.size());
            return true;
        }

        bool read(
            void* destination,
            uint32_t capacity,
            uint32_t& bytes,
            uint64_t& remote_steam_id,
            int channel) noexcept override
        {
            bytes = 0;
            remote_steam_id = 0;
            if (!destination) return false;
            std::lock_guard<std::mutex> lock(m_bus.mutex);
            const auto found = find_packet(channel);
            if (found == m_bus.packets.end()
                || found->bytes.size() > capacity)
            {
                return false;
            }
            bytes = static_cast<uint32_t>(found->bytes.size());
            remote_steam_id = found->sender;
            std::memcpy(destination, found->bytes.data(), bytes);
            m_bus.packets.erase(found);
            return true;
        }

        bool session_state(
            uint64_t remote_steam_id,
            Horse::RollbackSteamP2PSessionState& state) noexcept override
        {
            state = {};
            if (!available() || remote_steam_id == 0) return false;
            state.connection_active = 1;
            state.using_relay = m_using_relay ? 1 : 0;
            return true;
        }

        bool close_channel(
            uint64_t remote_steam_id,
            int channel) noexcept override
        {
            if (!available() || remote_steam_id == 0) return false;
            ++m_closed_channels;
            m_last_closed_channel = channel;
            return true;
        }

        const char* interface_version() const noexcept override
        {
            return "SteamNetworking005-fake";
        }

        void inject(
            uint64_t sender,
            int channel,
            const void* data,
            uint32_t bytes)
        {
            FakeSteamPacket packet {};
            packet.sender = sender;
            packet.destination = m_local_steam_id;
            packet.channel = channel;
            const auto* begin = static_cast<const uint8_t*>(data);
            packet.bytes.assign(begin, begin + bytes);
            std::lock_guard<std::mutex> lock(m_bus.mutex);
            m_bus.packets.push_back(std::move(packet));
        }

        uint64_t reliable_sends() const noexcept { return m_reliable_sends; }
        uint64_t unreliable_sends() const noexcept
        {
            return m_unreliable_sends;
        }
        uint64_t reliable_protocol_handshakes() const noexcept
        {
            return m_reliable_protocol_handshakes;
        }
        uint64_t closed_channels() const noexcept
        {
            return m_closed_channels;
        }
        int last_closed_channel() const noexcept
        {
            return m_last_closed_channel;
        }
        void fail_next_initializations(uint32_t count) noexcept
        {
            m_initialize_failures = count;
        }
        uint64_t initialize_calls() const noexcept
        {
            return m_initialize_calls;
        }

    private:
        std::deque<FakeSteamPacket>::iterator find_packet(int channel)
        {
            return std::find_if(
                m_bus.packets.begin(),
                m_bus.packets.end(),
                [this, channel](const FakeSteamPacket& packet) {
                    return packet.destination == m_local_steam_id
                        && packet.channel == channel;
                });
        }

        FakeSteamBus& m_bus;
        uint64_t m_local_steam_id {0};
        bool m_using_relay {false};
        bool m_initialized {false};
        uint32_t m_initialize_failures {0};
        uint64_t m_initialize_calls {0};
        uint64_t m_reliable_sends {0};
        uint64_t m_unreliable_sends {0};
        uint64_t m_reliable_protocol_handshakes {0};
        uint64_t m_closed_channels {0};
        int m_last_closed_channel {0};
    };

    bool wait_ready(
        Horse::RollbackSteamP2PTransport& a,
        Horse::RollbackSteamP2PTransport& b,
        uint32_t timeout_ms)
    {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (a.peer_ready() && b.peer_ready()) return true;
            std::this_thread::yield();
        }
        return false;
    }
}

int main()
{
    constexpr uint64_t kLobby = 9001;
    constexpr uint64_t kOwner = 111;
    constexpr uint64_t kPeer = 222;
    FakeSteamBus bus {};
    FakeSteamApi api_a(bus, kOwner, false);
    FakeSteamApi api_b(bus, kPeer, true);

    Horse::RollbackSteamSessionIdentity identity_a {};
    identity_a.lobby_id = kLobby;
    identity_a.owner_steam_id = kOwner;
    identity_a.local_steam_id = kOwner;
    identity_a.remote_steam_id = kPeer;
    identity_a.selection_hash = 0x1020304050607080ull;
    identity_a.lifecycle_serial = 1;
    identity_a.named_session = 0x1000;
    identity_a.session_info = 0x1100;
    identity_a.connect_manager = 0x1200;
    identity_a.active_connect = 0x1300;
    identity_a.active_transport = 0x13A8;
    identity_a.session_connection = 0x1400;
    identity_a.active_connect_state = 3;
    identity_a.active_connect_sub_state = 0;
    identity_a.named_session_valid = true;
    identity_a.transport_connected = true;
    Horse::RollbackSteamSessionIdentity identity_b = identity_a;
    identity_b.local_steam_id = kPeer;
    identity_b.remote_steam_id = kOwner;

    Horse::RollbackProductionConfig config_a {};
    config_a.enabled = true;
    config_a.transport_mode = Horse::RollbackTransportMode::SteamP2P;
    config_a.local_player_slot = 0;
    config_a.native_input_source_slot = 0;
    config_a.local_peer = 1;
    config_a.remote_peer = 2;
    config_a.expected_build_id = 0x5343364255494C44ull;
    config_a.expected_schema_id = 0xABCDEF1122334455ull;
    config_a.expected_native_stage_identity = 0x10003u;
    Horse::RollbackProductionConfig config_b = config_a;
    config_b.local_player_slot = 1;
    config_b.native_input_source_slot = 1;
    config_b.local_peer = 2;
    config_b.remote_peer = 1;

    Horse::RollbackSteamP2PTransport transport_a(api_a);
    Horse::RollbackSteamP2PTransport transport_b(api_b);
    const bool identities =
        transport_a.set_peer_identity(identity_a)
        && transport_b.set_peer_identity(identity_b);
    const bool started = identities
        && transport_a.start(config_a)
        && transport_b.start(config_b);
    const bool ready = started
        && wait_ready(transport_a, transport_b, 5000);

    const uint32_t payload = 0x1234ABCDu;
    const bool queued = ready && transport_a.enqueue(
        Horse::RollbackProtocolV2PacketType::Gekko,
        &payload,
        sizeof(payload),
        Horse::RollbackSequenceStamp::From(77));
    Horse::RollbackUdpMessage received {};
    bool delivered = false;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (queued && std::chrono::steady_clock::now() < deadline)
    {
        if (transport_b.dequeue(received))
        {
            delivered = received.packet_type
                    == Horse::RollbackProtocolV2PacketType::Gekko
                && received.payload_bytes == sizeof(payload)
                && std::memcmp(
                    received.payload.data(), &payload, sizeof(payload)) == 0;
            break;
        }
        std::this_thread::yield();
    }
    const Horse::RollbackUdpWorkerStatus delivery_status_a =
        transport_a.status();
    const Horse::RollbackUdpWorkerStatus delivery_status_b =
        transport_b.status();
    const bool ready_status_overlay =
        delivery_status_a.transport_lifecycle
            == Horse::RollbackTransportLifecycle::Ready
        && delivery_status_b.transport_lifecycle
            == Horse::RollbackTransportLifecycle::Ready
        && delivery_status_a.bootstrap_attempt == 1
        && delivery_status_b.bootstrap_attempt == 1
        && delivery_status_a.bootstrap_attempt_limit
            == Horse::kRollbackSteamBootstrapMaxAttempts
        && delivery_status_b.bootstrap_attempt_limit
            == Horse::kRollbackSteamBootstrapMaxAttempts
        && delivery_status_a.bound_native_epoch_key
            == identity_a.native_epoch_key()
        && delivery_status_b.bound_native_epoch_key
            == identity_b.native_epoch_key();

    const uint32_t foreign = 0xDEADBEEFu;
    const uint64_t rejected_before =
        transport_b.status().packets_rejected;
    api_b.inject(
        333,
        Horse::kRollbackSteamP2PChannel,
        &foreign,
        sizeof(foreign));
    bool foreign_rejected = false;
    const auto reject_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < reject_deadline)
    {
        if (transport_b.status().packets_rejected > rejected_before)
        {
            foreign_rejected = true;
            break;
        }
        std::this_thread::yield();
    }

    const bool relay_state =
        !transport_a.using_relay() && transport_b.using_relay();
    const bool interface_reused = transport_a.interface_revision() == 5;
    const bool reliable_bootstrap =
        api_a.reliable_sends() != 0 && api_b.reliable_sends() != 0;
    const bool no_redundant_protocol_handshake =
        api_a.reliable_protocol_handshakes() == 0
        && api_b.reliable_protocol_handshakes() == 0;
    const bool unreliable_gameplay =
        api_a.unreliable_sends() != 0;
    // The fake intentionally exposes no acceptance API. A successful
    // bootstrap therefore proves Horse reused SC6's already accepted native
    // session instead of taking a second ownership path.
    const bool native_session_owned_by_sc6 = ready;

    transport_a.stop();
    transport_b.stop();
    const bool channel_only_shutdown =
        api_a.closed_channels() == 1
        && api_b.closed_channels() == 1
        && api_a.last_closed_channel()
            == Horse::kRollbackSteamP2PChannel
        && api_b.last_closed_channel()
            == Horse::kRollbackSteamP2PChannel;

    Horse::RollbackSteamP2PWireEndpoint probe_endpoint(api_a);
    const bool probe_opened =
        probe_endpoint.set_identity(identity_a)
        && probe_endpoint.open(config_a);
    const std::array<uint8_t,
        Horse::kRollbackProtocolV2MaxWireBytes + 1> oversized {};
    const bool oversized_rejected = probe_opened
        && !probe_endpoint.send_raw(oversized.data(), oversized.size());
    Horse::RollbackProtocolV2Header hello {};
    hello.packet_type = Horse::RollbackProtocolV2PacketType::Hello;
    hello.source_peer = config_a.local_peer;
    hello.destination_peer = config_a.remote_peer;
    hello.sequence = 1;
    hello.build_id = config_a.expected_build_id;
    hello.schema_id = config_a.expected_schema_id;
    Horse::RollbackProtocolV2WirePacket hello_wire {};
    const bool fallback_handshake_reliable =
        probe_opened
        && Horse::EncodeRollbackProtocolV2Packet(
            hello,
            nullptr,
            0,
            "0123456789abcdef",
            hello_wire)
        && probe_endpoint.send(hello_wire)
        && api_a.reliable_protocol_handshakes() == 1;
    probe_endpoint.close();

    FakeSteamBus retry_bus {};
    FakeSteamApi retry_api(retry_bus, kOwner, false);
    retry_api.fail_next_initializations(1);
    Horse::RollbackSteamP2PTransport retry_transport(retry_api);
    const bool retry_identity =
        retry_transport.set_peer_identity(identity_a);
    const bool retry_start_dispatched =
        retry_identity && retry_transport.start(config_a);
    const auto retry_failure_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (retry_api.initialize_calls() != 1
        && std::chrono::steady_clock::now() < retry_failure_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto retry_success_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (retry_api.initialize_calls() < 2
        && std::chrono::steady_clock::now() < retry_success_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool initialization_retried =
        retry_start_dispatched
        && retry_api.initialize_calls() >= 2
        && retry_transport.bootstrap_attempt() == 2
        && !retry_transport.retry_exhausted();
    retry_transport.stop();

    FakeSteamBus exhaustion_bus {};
    FakeSteamApi exhaustion_api(exhaustion_bus, kOwner, false);
    exhaustion_api.fail_next_initializations(
        Horse::kRollbackSteamBootstrapMaxAttempts);
    Horse::RollbackSteamP2PTransport exhaustion_transport(exhaustion_api);
    const bool exhaustion_started =
        exhaustion_transport.set_peer_identity(identity_a)
        && exhaustion_transport.start(config_a);
    bool status_poll_coherent = true;
    Horse::RollbackUdpWorkerStatus exhaustion_status {};
    const auto exhaustion_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < exhaustion_deadline)
    {
        exhaustion_status = exhaustion_transport.status();
        status_poll_coherent = status_poll_coherent
            && exhaustion_status.bootstrap_attempt
                <= exhaustion_status.bootstrap_attempt_limit
            && exhaustion_status.bound_native_epoch_key
                == identity_a.native_epoch_key();
        if (exhaustion_status.transport_lifecycle
            == Horse::RollbackTransportLifecycle::Failed)
            break;
        std::this_thread::yield();
    }
    const bool retry_exhaustion_published = exhaustion_started
        && status_poll_coherent
        && exhaustion_status.transport_lifecycle
            == Horse::RollbackTransportLifecycle::Failed
        && !exhaustion_status.running
        && exhaustion_status.retry_exhausted
        && exhaustion_status.bootstrap_attempt
            == Horse::kRollbackSteamBootstrapMaxAttempts
        && exhaustion_status.bootstrap_attempt_limit
            == Horse::kRollbackSteamBootstrapMaxAttempts
        && exhaustion_status.failure
            == Horse::RollbackUdpWorkerFailure::EndpointOpenFailed
        && exhaustion_status.last_failure
            == Horse::RollbackUdpWorkerFailure::EndpointOpenFailed;
    exhaustion_transport.stop();

    FakeSteamBus stop_delay_bus {};
    FakeSteamApi stop_delay_api(stop_delay_bus, kOwner, false);
    stop_delay_api.fail_next_initializations(1);
    Horse::RollbackSteamP2PTransport stop_delay_transport(stop_delay_api);
    const bool stop_delay_started =
        stop_delay_transport.set_peer_identity(identity_a)
        && stop_delay_transport.start(config_a);
    const auto retry_delay_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(1);
    while (stop_delay_transport.status().transport_lifecycle
            != Horse::RollbackTransportLifecycle::RetryDelay
        && std::chrono::steady_clock::now() < retry_delay_deadline)
    {
        std::this_thread::yield();
    }
    const auto stop_delay_begin = std::chrono::steady_clock::now();
    stop_delay_transport.stop();
    const auto stop_delay_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - stop_delay_begin).count();
    const bool stop_interrupts_retry_delay = stop_delay_started
        && stop_delay_elapsed < 100
        && stop_delay_transport.status().transport_lifecycle
            == Horse::RollbackTransportLifecycle::Stopped;

    const bool fatal_retry_classification =
        !Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::InvalidConfig)
        && !Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::AuthenticationFailed)
        && !Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::NonceGenerationFailed)
        && !Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::ResourceAllocationFailed)
        && !Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::PeerSessionChanged)
        && Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::EndpointOpenFailed)
        && Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::EndpointIoFailed)
        && Horse::RollbackSteamBootstrapFailureRetryable(
            Horse::RollbackUdpWorkerFailure::PeerTimeout);

    FakeSteamBus shutdown_bus {};
    FakeSteamApi shutdown_api_a(shutdown_bus, kOwner, false);
    FakeSteamApi shutdown_api_b(shutdown_bus, kPeer, false);
    Horse::RollbackSteamP2PTransport shutdown_a(shutdown_api_a);
    Horse::RollbackSteamP2PTransport shutdown_b(shutdown_api_b);
    const bool shutdown_started =
        shutdown_a.set_peer_identity(identity_a)
        && shutdown_b.set_peer_identity(identity_b)
        && shutdown_a.start(config_a)
        && shutdown_b.start(config_b);
    std::thread stop_a([&shutdown_a]() noexcept {
        shutdown_a.stop();
    });
    std::thread stop_b([&shutdown_b]() noexcept {
        shutdown_b.stop();
    });
    stop_a.join();
    stop_b.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool concurrent_bootstrap_shutdown =
        shutdown_started
        && !shutdown_a.status().running
        && !shutdown_b.status().running
        && !shutdown_a.status().endpoint_open
        && !shutdown_b.status().endpoint_open
        && !shutdown_a.key_confirmed()
        && !shutdown_b.key_confirmed()
        && !shutdown_a.peer_ready()
        && !shutdown_b.peer_ready()
        && !shutdown_a.enqueue(
            Horse::RollbackProtocolV2PacketType::Gekko,
            &payload,
            sizeof(payload));

    const bool passed = identities
        && started
        && ready
        && transport_a.key_confirmed() == false
        && transport_b.key_confirmed() == false
        && delivered
        && foreign_rejected
        && oversized_rejected
        && relay_state
        && interface_reused
        && reliable_bootstrap
        && no_redundant_protocol_handshake
        && fallback_handshake_reliable
        && unreliable_gameplay
        && native_session_owned_by_sc6
        && ready_status_overlay
        && channel_only_shutdown
        && initialization_retried
        && retry_exhaustion_published
        && stop_interrupts_retry_delay
        && fatal_retry_classification
        && concurrent_bootstrap_shutdown;
    std::printf(
        "rollback_steam_p2p_transport_selftest=%s "
        "started=%u ready=%u delivered=%u foreign_rejected=%u "
        "relay=%u reliable_bootstrap=%u channel_only_shutdown=%u "
        "initialization_retried=%u concurrent_bootstrap_shutdown=%u "
        "retry_exhaustion=%u stop_interrupts_retry_delay=%u "
        "fatal_retry_classification=%u "
        "exhaustion_lifecycle_running_retry_attempt_limit_fail_last="
        "%u/%u/%u/%u/%u/%u/%u "
        "a_sent_recv_auth_reject_decode_route_replay_gen_fail="
        "%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%u "
        "b_sent_recv_auth_reject_decode_route_replay_gen_fail="
        "%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%u\n",
        passed ? "PASS" : "FAIL",
        started ? 1u : 0u,
        ready ? 1u : 0u,
        delivered ? 1u : 0u,
        foreign_rejected ? 1u : 0u,
        relay_state ? 1u : 0u,
        reliable_bootstrap ? 1u : 0u,
        channel_only_shutdown ? 1u : 0u,
        initialization_retried ? 1u : 0u,
        concurrent_bootstrap_shutdown ? 1u : 0u,
        retry_exhaustion_published ? 1u : 0u,
        stop_interrupts_retry_delay ? 1u : 0u,
        fatal_retry_classification ? 1u : 0u,
        static_cast<unsigned>(exhaustion_status.transport_lifecycle),
        exhaustion_status.running ? 1u : 0u,
        exhaustion_status.retry_exhausted ? 1u : 0u,
        exhaustion_status.bootstrap_attempt,
        exhaustion_status.bootstrap_attempt_limit,
        static_cast<unsigned>(exhaustion_status.failure),
        static_cast<unsigned>(exhaustion_status.last_failure),
        static_cast<unsigned long long>(delivery_status_a.packets_sent),
        static_cast<unsigned long long>(delivery_status_a.packets_received),
        static_cast<unsigned long long>(
            delivery_status_a.packets_authenticated),
        static_cast<unsigned long long>(delivery_status_a.packets_rejected),
        static_cast<unsigned long long>(
            delivery_status_a.packets_decode_rejected),
        static_cast<unsigned long long>(
            delivery_status_a.packets_route_rejected),
        static_cast<unsigned long long>(
            delivery_status_a.packets_replay_rejected),
        static_cast<unsigned long long>(
            delivery_status_a.handshake_generation),
        static_cast<unsigned>(delivery_status_a.failure),
        static_cast<unsigned long long>(delivery_status_b.packets_sent),
        static_cast<unsigned long long>(delivery_status_b.packets_received),
        static_cast<unsigned long long>(
            delivery_status_b.packets_authenticated),
        static_cast<unsigned long long>(delivery_status_b.packets_rejected),
        static_cast<unsigned long long>(
            delivery_status_b.packets_decode_rejected),
        static_cast<unsigned long long>(
            delivery_status_b.packets_route_rejected),
        static_cast<unsigned long long>(
            delivery_status_b.packets_replay_rejected),
        static_cast<unsigned long long>(
            delivery_status_b.handshake_generation),
        static_cast<unsigned>(delivery_status_b.failure));
    return passed ? 0 : 1;
}
