#include "../HorseMod/horselib/RollbackUdpRuntime.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
    class RollbackUdpSelfTestPortLock
    {
    public:
        bool acquire() noexcept
        {
            m_mutex = CreateMutexW(
                nullptr, FALSE,
                L"Local\\HorseModRollbackUdpRuntimeSelfTestPortsV1");
            if (m_mutex == nullptr) return false;
            const DWORD result = WaitForSingleObject(m_mutex, 60000);
            m_acquired =
                result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
            return m_acquired;
        }

        ~RollbackUdpSelfTestPortLock()
        {
            if (m_acquired) ReleaseMutex(m_mutex);
            if (m_mutex != nullptr) CloseHandle(m_mutex);
        }

        RollbackUdpSelfTestPortLock(
            const RollbackUdpSelfTestPortLock&) = delete;
        RollbackUdpSelfTestPortLock& operator=(
            const RollbackUdpSelfTestPortLock&) = delete;

        RollbackUdpSelfTestPortLock() = default;

    private:
        HANDLE m_mutex {nullptr};
        bool m_acquired {false};
    };

    bool wait_ready(
        Horse::RollbackUdpNetworkWorker& a,
        Horse::RollbackUdpNetworkWorker& b,
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

    bool wait_profile_rejected(
        Horse::RollbackUdpNetworkWorker& a,
        Horse::RollbackUdpNetworkWorker& b,
        uint32_t timeout_ms,
        bool& became_ready)
    {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline)
        {
            became_ready = became_ready || a.peer_ready() || b.peer_ready();
            const auto status_a = a.status();
            const auto status_b = b.status();
            if (status_a.packets_rejected > 0
                && status_b.packets_rejected > 0)
            {
                return true;
            }
            std::this_thread::yield();
        }
        return false;
    }

    bool profile_pair_rejected(
        const Horse::RollbackProductionConfig& config_a,
        const Horse::RollbackProductionConfig& config_b)
    {
        Horse::RollbackUdpNetworkWorker worker_a {};
        Horse::RollbackUdpNetworkWorker worker_b {};
        if (!worker_a.start(config_a) || !worker_b.start(config_b))
        {
            worker_a.stop();
            worker_b.stop();
            return false;
        }
        bool became_ready = false;
        const bool rejected = wait_profile_rejected(
            worker_a, worker_b, 2000, became_ready);
        const auto status_a = worker_a.status();
        const auto status_b = worker_b.status();
        worker_a.stop();
        worker_b.stop();
        return rejected
            && !became_ready
            && !status_a.peer_ready
            && !status_b.peer_ready;
    }
}

int main()
{
    // This executable intentionally uses a fixed suite of loopback ports so
    // every contract-mismatch case can name both endpoints deterministically.
    // Fresh ON/OFF qualification matrices may invoke this self-test from
    // separate CTest processes at the same time, so protect the complete port
    // suite with a machine-local cross-process lock.
    RollbackUdpSelfTestPortLock port_lock {};
    if (!port_lock.acquire())
    {
        std::fprintf(
            stderr,
            "rollback udp-runtime self-test failed to acquire port-suite lock\n");
        return 1;
    }

    constexpr uint16_t kPortA = 65420;
    constexpr uint16_t kPortB = 65421;
    Horse::RollbackProductionConfig a {};
    a.enabled = true;
    a.bind_address = "127.0.0.1";
    a.bind_port = kPortA;
    a.peer_address = "127.0.0.1";
    a.peer_port = kPortB;
    a.local_peer = 1;
    a.remote_peer = 2;
    a.secret = "rollback-udp-runtime-self-test";
    a.expected_build_id = 0x5343364255494C44ull;
    a.expected_schema_id = 0xABCDEF1122334455ull;
    a.expected_native_stage_identity = 0x10003u;
    Horse::RollbackProductionConfig b = a;
    b.bind_port = kPortB;
    b.peer_port = kPortA;
    b.local_peer = 2;
    b.remote_peer = 1;
    b.local_player_slot = 1;
    b.native_input_source_slot = 1;

    const Horse::RollbackProductionConfig equivalent_a = a;
    Horse::RollbackProductionConfig changed_secret = a;
    changed_secret.secret += "-changed";
    Horse::RollbackProductionConfig changed_fixture = a;
    changed_fixture.deterministic_input.enabled = true;
    Horse::RollbackProductionConfig changed_profile = a;
    changed_profile.network_profile =
        Horse::RollbackNetworkProfileKind::Wifi50msJitter;
    Horse::RollbackProductionConfig changed_worker_stall = a;
    changed_worker_stall.test_worker_stall_after_ms = 100;
    changed_worker_stall.test_worker_stall_duration_ms = 125;
    Horse::RollbackProductionConfig changed_domain = a;
    changed_domain.session_domain =
        Horse::RollbackSessionDomain::ReplayForkLab;
    Horse::RollbackProductionConfig changed_stage = a;
    changed_stage.expected_native_stage_identity = 0x10004u;
    Horse::RollbackProductionConfig changed_selection_binding = a;
    changed_selection_binding.bind_observed_stock_selection = true;
    Horse::RollbackProductionConfig changed_test_selection_override = a;
    changed_test_selection_override.replay_test_selection_override = true;
    const bool invalid_test_selection_override =
        !changed_test_selection_override.valid();
    Horse::RollbackProductionConfig valid_test_selection_override =
        changed_test_selection_override;
    valid_test_selection_override.replay_input.enabled = true;
    valid_test_selection_override.deterministic_input.enabled = true;
    valid_test_selection_override.replay_input_file = "fixture.bin";
    valid_test_selection_override.replay_input.file_sha256[0] = 1;
    valid_test_selection_override.replay_input.replay_random_seed = 1;
    valid_test_selection_override.replay_input.round_start_count = 1;
    valid_test_selection_override.expected_selection_hash = 0x1234;
    const bool test_selection_override_scope =
        invalid_test_selection_override
        && valid_test_selection_override.valid();
    Horse::RollbackProductionConfig changed_replay_input = a;
    changed_replay_input.replay_input.enabled = true;
    changed_replay_input.replay_input_file = "fixture.bin";
    changed_replay_input.replay_input.file_sha256[0] = 1;
    Horse::RollbackProductionConfig changed_trace_detail = a;
    changed_trace_detail.replay_trace_input_only = true;
    const bool config_equivalence = test_selection_override_scope
        &&
        Horse::RollbackProductionConfigEquivalent(a, equivalent_a)
        && !Horse::RollbackProductionConfigEquivalent(a, b)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_secret)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_fixture)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_profile)
        && !Horse::RollbackProductionConfigEquivalent(
            a, changed_worker_stall)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_domain)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_stage)
        && !Horse::RollbackProductionConfigEquivalent(
            a, changed_selection_binding)
        && !Horse::RollbackProductionConfigEquivalent(
            a, changed_test_selection_override)
        && !Horse::RollbackProductionConfigEquivalent(
            a, changed_replay_input)
        && !Horse::RollbackProductionConfigEquivalent(
            a, changed_trace_detail);
    const uint64_t contract_hash =
        Horse::ComputeRollbackSessionContractHash(a, 0x12345678ull);
    const bool session_contract_hash = contract_hash != 0
        && contract_hash != Horse::ComputeRollbackSessionContractHash(
            changed_selection_binding, 0x12345678ull)
        && contract_hash != Horse::ComputeRollbackSessionContractHash(
            changed_test_selection_override, 0x12345678ull)
        && contract_hash != Horse::ComputeRollbackSessionContractHash(
            a, 0x12345679ull);

    Horse::RollbackUdpNetworkWorker worker_a {};
    Horse::RollbackUdpNetworkWorker worker_b {};
    const bool started = worker_a.start(a) && worker_b.start(b);
    const bool ready = started && wait_ready(worker_a, worker_b, 3000);
    const uint32_t payload = 0x1234ABCDu;
    const bool queued = ready && worker_a.enqueue(
        Horse::RollbackProtocolV2PacketType::Gekko,
        &payload,
        sizeof(payload),
        Horse::RollbackSequenceStamp::From(77));

    Horse::RollbackUdpMessage received {};
    bool delivered = false;
    const auto delivery_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (queued && std::chrono::steady_clock::now() < delivery_deadline)
    {
        if (worker_b.dequeue(received))
        {
            delivered = received.packet_type
                    == Horse::RollbackProtocolV2PacketType::Gekko
                && received.payload_bytes == sizeof(payload)
                && std::memcmp(
                    received.payload.data(), &payload, sizeof(payload)) == 0
                && received.ack.valid
                && received.ack.value == 77;
            break;
        }
        std::this_thread::yield();
    }

    const auto status_ready_a = worker_a.status();
    const auto status_b = worker_b.status();
    worker_b.stop();
    const auto timeout_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (worker_a.peer_ready()
           && std::chrono::steady_clock::now() < timeout_deadline)
    {
        std::this_thread::yield();
    }
    const auto status_a = worker_a.status();
    const bool heartbeat_expired =
        !status_a.peer_ready
        && status_a.failure == Horse::RollbackUdpWorkerFailure::PeerTimeout;
    worker_a.stop();
    const auto stopped_a = worker_a.status();
    const auto stopped_b = worker_b.status();
    const bool cleanup = !stopped_a.running && !stopped_b.running
        && !stopped_a.endpoint_open && !stopped_b.endpoint_open;

    // Both peers claiming slot zero is a validly authenticated transport
    // exchange, but it is not a compatible gameplay handshake. Each side
    // must reject the signed profile and remain unready.
    Horse::RollbackProductionConfig mismatch_a = a;
    mismatch_a.bind_port = 65422;
    mismatch_a.peer_port = 65423;
    mismatch_a.local_player_slot = 0;
    Horse::RollbackProductionConfig mismatch_b = mismatch_a;
    mismatch_b.bind_port = mismatch_a.peer_port;
    mismatch_b.peer_port = mismatch_a.bind_port;
    mismatch_b.local_peer = mismatch_a.remote_peer;
    mismatch_b.remote_peer = mismatch_a.local_peer;
    mismatch_b.local_player_slot = 0;

    Horse::RollbackUdpNetworkWorker mismatch_worker_a {};
    Horse::RollbackUdpNetworkWorker mismatch_worker_b {};
    const bool mismatch_started = mismatch_worker_a.start(mismatch_a)
        && mismatch_worker_b.start(mismatch_b);
    bool mismatch_became_ready = false;
    const bool mismatch_rejected = mismatch_started
        && wait_profile_rejected(
            mismatch_worker_a,
            mismatch_worker_b,
            2000,
            mismatch_became_ready);
    const auto mismatch_status_a = mismatch_worker_a.status();
    const auto mismatch_status_b = mismatch_worker_b.status();
    const bool mismatch_stayed_unready = !mismatch_became_ready
        && !mismatch_status_a.peer_ready
        && !mismatch_status_b.peer_ready
        && !mismatch_status_a.endpoint_pinned
        && !mismatch_status_b.endpoint_pinned;
    mismatch_worker_a.stop();
    mismatch_worker_b.stop();
    const auto mismatch_stopped_a = mismatch_worker_a.status();
    const auto mismatch_stopped_b = mismatch_worker_b.status();
    const bool mismatch_cleanup = !mismatch_stopped_a.running
        && !mismatch_stopped_b.running
        && !mismatch_stopped_a.endpoint_open
        && !mismatch_stopped_b.endpoint_open;

    Horse::RollbackProductionConfig profile_a = a;
    profile_a.bind_port = 65426;
    profile_a.peer_port = 65427;
    Horse::RollbackProductionConfig profile_b = b;
    profile_b.bind_port = profile_a.peer_port;
    profile_b.peer_port = profile_a.bind_port;

    Horse::RollbackProductionConfig fixture_mismatch_b = profile_b;
    fixture_mismatch_b.deterministic_input.enabled = true;
    const bool fixture_mismatch_rejected = profile_pair_rejected(
        profile_a, fixture_mismatch_b);

    Horse::RollbackProductionConfig replay_input_a = profile_a;
    Horse::RollbackProductionConfig replay_input_b = profile_b;
    replay_input_a.deterministic_input.enabled = true;
    replay_input_b.deterministic_input.enabled = true;
    replay_input_a.replay_input.enabled = true;
    replay_input_b.replay_input.enabled = true;
    replay_input_a.replay_input_file = "fixture.bin";
    replay_input_b.replay_input_file = "fixture.bin";
    replay_input_a.replay_input.replay_random_seed = 1;
    replay_input_b.replay_input.replay_random_seed = 1;
    replay_input_a.replay_input.round_start_count = 1;
    replay_input_b.replay_input.round_start_count = 1;
    replay_input_a.replay_input.file_sha256[0] = 1;
    replay_input_b.replay_input.file_sha256[0] = 2;
    const bool replay_input_mismatch_rejected = profile_pair_rejected(
        replay_input_a, replay_input_b);

    profile_a.bind_port = 65434;
    profile_a.peer_port = 65435;
    profile_b.bind_port = profile_a.peer_port;
    profile_b.peer_port = profile_a.bind_port;
    profile_a.network_profile =
        Horse::RollbackNetworkProfileKind::Wifi50msJitter;
    profile_b.network_profile =
        Horse::RollbackNetworkProfileKind::Clean0ms;
    const bool network_profile_mismatch_rejected = profile_pair_rejected(
        profile_a, profile_b);
    // Exercise the production wire scheduler, not only the in-process model.
    // Handshake and gameplay datagrams must traverse the same deterministic
    // delay/jitter/reorder path and still authenticate end to end.
    Horse::RollbackProductionConfig wifi_a = a;
    wifi_a.bind_port = 65436;
    wifi_a.peer_port = 65437;
    wifi_a.network_profile =
        Horse::RollbackNetworkProfileKind::Wifi50msJitter;
    wifi_a.fault_seed = 0x57494649u;
    Horse::RollbackProductionConfig wifi_b = b;
    wifi_b.bind_port = wifi_a.peer_port;
    wifi_b.peer_port = wifi_a.bind_port;
    wifi_b.network_profile = wifi_a.network_profile;
    wifi_b.fault_seed = wifi_a.fault_seed;
    Horse::RollbackUdpNetworkWorker wifi_worker_a {};
    Horse::RollbackUdpNetworkWorker wifi_worker_b {};
    const bool wifi_started = wifi_worker_a.start(wifi_a)
        && wifi_worker_b.start(wifi_b);
    const bool wifi_ready = wifi_started
        && wait_ready(wifi_worker_a, wifi_worker_b, 5000);
    bool wifi_queued = wifi_ready;
    for (uint32_t i = 0; wifi_queued && i < 64; ++i)
    {
        wifi_queued = wifi_worker_a.enqueue(
            Horse::RollbackProtocolV2PacketType::Gekko,
            &i,
            sizeof(i),
            Horse::RollbackSequenceStamp::From(i));
    }
    uint32_t wifi_received = 0;
    const auto wifi_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    Horse::RollbackUdpMessage wifi_message {};
    while (wifi_queued && std::chrono::steady_clock::now() < wifi_deadline)
    {
        while (wifi_worker_b.dequeue(wifi_message)) ++wifi_received;
        const auto current = wifi_worker_a.status();
        if (wifi_received >= 64 && current.fault_packets_delivered > 0)
            break;
        std::this_thread::yield();
    }
    const auto wifi_status_a = wifi_worker_a.status();
    const auto wifi_status_b = wifi_worker_b.status();
    const bool wifi_fault_path_observed = wifi_received >= 64
        && wifi_status_a.fault_packets_submitted > 0
        && wifi_status_a.fault_packets_queued > 0
        && wifi_status_a.fault_packets_delivered > 0
        && wifi_status_a.network_profile
            == Horse::RollbackNetworkProfileKind::Wifi50msJitter
        && wifi_status_a.fault_seed == wifi_a.fault_seed
        && wifi_status_b.packets_authenticated > 0;
    wifi_worker_a.stop();
    wifi_worker_b.stop();

    // The test-only schedule stalls exactly one common transport worker,
    // publishes bounded start/completion telemetry, and then recovers the
    // authenticated session without changing the peer handshake profile.
    Horse::RollbackProductionConfig stall_a = a;
    stall_a.bind_port = 65452;
    stall_a.peer_port = 65453;
    stall_a.test_worker_stall_after_ms = 100;
    stall_a.test_worker_stall_duration_ms = 125;
    Horse::RollbackProductionConfig stall_b = b;
    stall_b.bind_port = stall_a.peer_port;
    stall_b.peer_port = stall_a.bind_port;
    Horse::RollbackUdpNetworkWorker stall_worker_a {};
    Horse::RollbackUdpNetworkWorker stall_worker_b {};
    const bool stall_started = stall_worker_a.start(stall_a)
        && stall_worker_b.start(stall_b);
    const bool stall_ready = stall_started
        && wait_ready(stall_worker_a, stall_worker_b, 3000);
    const auto stall_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (stall_worker_a.status().test_worker_stalls_completed == 0
        && std::chrono::steady_clock::now() < stall_deadline)
    {
        std::this_thread::yield();
    }
    const auto stall_status_a = stall_worker_a.status();
    const auto stall_status_b = stall_worker_b.status();
    const bool stall_telemetry = stall_ready
        && stall_status_a.test_worker_stalls_started == 1
        && stall_status_a.test_worker_stalls_completed == 1
        && stall_status_a.test_worker_stall_actual_ms >= 105
        && stall_status_a.test_worker_stall_actual_ms <= 375
        && stall_status_b.test_worker_stalls_started == 0
        && stall_status_b.test_worker_stalls_completed == 0
        && stall_status_b.test_worker_stall_actual_ms == 0
        && stall_worker_a.peer_ready()
        && stall_worker_b.peer_ready();
    stall_worker_a.stop();
    stall_worker_b.stop();

    Horse::RollbackProductionConfig half_stall = a;
    half_stall.test_worker_stall_after_ms = 100;
    Horse::RollbackProductionConfig oversized_stall = a;
    oversized_stall.test_worker_stall_after_ms = 100;
    oversized_stall.test_worker_stall_duration_ms = 1001;
    const bool invalid_stall_configs =
        !half_stall.valid() && !oversized_stall.valid();

    Horse::RollbackProductionConfig replay_a = a;
    replay_a.bind_port = 65438;
    replay_a.peer_port = 65439;
    replay_a.session_domain = Horse::RollbackSessionDomain::ReplayForkLab;
    replay_a.replay_anchor_sequence = 2751;
    replay_a.replay_anchor_round = 1;
    replay_a.replay_anchor_master = 414;
    replay_a.replay_run_nonce_hash = 0x52464C41424E4F4Eull;
    for (size_t i = 0; i < replay_a.replay_sha256.size(); ++i)
        replay_a.replay_sha256[i] = static_cast<uint8_t>(i + 1u);
    Horse::RollbackProductionConfig replay_b = replay_a;
    replay_b.bind_port = replay_a.peer_port;
    replay_b.peer_port = replay_a.bind_port;
    replay_b.local_peer = replay_a.remote_peer;
    replay_b.remote_peer = replay_a.local_peer;
    replay_b.local_player_slot = 1;
    replay_b.native_input_source_slot = 1;
    Horse::RollbackUdpNetworkWorker replay_worker_a {};
    Horse::RollbackUdpNetworkWorker replay_worker_b {};
    const bool replay_started = replay_worker_a.start(replay_a)
        && replay_worker_b.start(replay_b);
    const bool replay_ready = replay_started
        && wait_ready(replay_worker_a, replay_worker_b, 3000);
    replay_worker_a.stop();
    replay_worker_b.stop();

    replay_a.bind_port = 65440;
    replay_a.peer_port = 65441;
    replay_b.bind_port = replay_a.peer_port;
    replay_b.peer_port = replay_a.bind_port;
    Horse::RollbackProductionConfig replay_hash_mismatch_b = replay_b;
    replay_hash_mismatch_b.replay_sha256[0] ^= 0xFFu;
    const bool replay_hash_mismatch_rejected = profile_pair_rejected(
        replay_a, replay_hash_mismatch_b);

    replay_a.bind_port = 65442;
    replay_a.peer_port = 65443;
    replay_b.bind_port = replay_a.peer_port;
    replay_b.peer_port = replay_a.bind_port;
    Horse::RollbackProductionConfig replay_anchor_mismatch_b = replay_b;
    ++replay_anchor_mismatch_b.replay_anchor_master;
    const bool replay_anchor_mismatch_rejected = profile_pair_rejected(
        replay_a, replay_anchor_mismatch_b);

    replay_a.bind_port = 65444;
    replay_a.peer_port = 65445;
    replay_b.bind_port = replay_a.peer_port;
    replay_b.peer_port = replay_a.bind_port;
    Horse::RollbackProductionConfig replay_nonce_mismatch_b = replay_b;
    ++replay_nonce_mismatch_b.replay_run_nonce_hash;
    const bool replay_nonce_mismatch_rejected = profile_pair_rejected(
        replay_a, replay_nonce_mismatch_b);

    replay_a.bind_port = 65446;
    replay_a.peer_port = 65447;
    replay_b.bind_port = replay_a.peer_port;
    replay_b.peer_port = replay_a.bind_port;
    replay_b.session_domain = Horse::RollbackSessionDomain::Production;
    const bool replay_domain_mismatch_rejected = profile_pair_rejected(
        replay_a, replay_b);

    replay_a.bind_port = 65448;
    replay_a.peer_port = 65449;
    replay_b = replay_a;
    replay_b.bind_port = replay_a.peer_port;
    replay_b.peer_port = replay_a.bind_port;
    replay_b.local_peer = replay_a.remote_peer;
    replay_b.remote_peer = replay_a.local_peer;
    replay_b.local_player_slot = 1;
    replay_b.native_input_source_slot = 1;
    ++replay_b.expected_build_id;
    const bool replay_build_mismatch_rejected = profile_pair_rejected(
        replay_a, replay_b);

    replay_a.bind_port = 65450;
    replay_a.peer_port = 65451;
    replay_b = replay_a;
    replay_b.bind_port = replay_a.peer_port;
    replay_b.peer_port = replay_a.bind_port;
    replay_b.local_peer = replay_a.remote_peer;
    replay_b.remote_peer = replay_a.local_peer;
    replay_b.local_player_slot = 1;
    replay_b.native_input_source_slot = 1;
    ++replay_b.expected_schema_id;
    const bool replay_schema_mismatch_rejected = profile_pair_rejected(
        replay_a, replay_b);

    Horse::RollbackProductionConfig wrong_native_source = b;
    wrong_native_source.native_input_source_slot = 0;
    Horse::RollbackUdpNetworkWorker wrong_native_source_worker {};
    const bool wrong_native_source_rejected =
        !wrong_native_source.valid()
        && !wrong_native_source_worker.start(wrong_native_source)
        && wrong_native_source_worker.status().failure
            == Horse::RollbackUdpWorkerFailure::InvalidConfig;
    wrong_native_source_worker.stop();

    Horse::RollbackProductionConfig invalid_peer_address = a;
    invalid_peer_address.peer_address = "not-an-ipv4-endpoint";
    Horse::RollbackUdpNetworkWorker invalid_peer_address_worker {};
    Horse::IRollbackTransport* invalid_peer_transport =
        &invalid_peer_address_worker;
    const bool invalid_peer_address_rejected =
        !invalid_peer_transport->start(invalid_peer_address)
        && invalid_peer_transport->status().failure
            == Horse::RollbackUdpWorkerFailure::InvalidConfig;
    invalid_peer_transport->stop();
    sockaddr_in resolved_localhost {};
    const bool dns_peer_resolved =
        Horse::RollbackUdpEndpoint::parse_address(
            "localhost", 47170, resolved_localhost)
        && resolved_localhost.sin_family == AF_INET
        && ntohs(resolved_localhost.sin_port) == 47170
        && resolved_localhost.sin_addr.s_addr != 0;

    // Fill a live authenticated inbound queue while deliberately withholding
    // the game-thread drain. The receiver must latch QueueOverflow and stop;
    // no later handshake packet may downgrade that fatal state.
    Horse::RollbackProductionConfig flood_a = a;
    flood_a.bind_port = 65424;
    flood_a.peer_port = 65425;
    Horse::RollbackProductionConfig flood_b = b;
    flood_b.bind_port = flood_a.peer_port;
    flood_b.peer_port = flood_a.bind_port;
    Horse::RollbackUdpNetworkWorker flood_worker_a {};
    Horse::RollbackUdpNetworkWorker flood_worker_b {};
    const bool flood_started = flood_worker_a.start(flood_a)
        && flood_worker_b.start(flood_b);
    const bool flood_ready = flood_started
        && wait_ready(flood_worker_a, flood_worker_b, 3000);
    bool flood_sender_queued = flood_ready;
    for (uint32_t i = 0; flood_sender_queued && i < 320; ++i)
    {
        flood_sender_queued = flood_worker_a.enqueue(
            Horse::RollbackProtocolV2PacketType::Gekko,
            &i,
            sizeof(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto flood_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (flood_worker_b.status().failure
                != Horse::RollbackUdpWorkerFailure::QueueOverflow
           && std::chrono::steady_clock::now() < flood_deadline)
    {
        std::this_thread::yield();
    }
    const auto flood_status_b = flood_worker_b.status();
    const bool inbound_overflow_failed_closed = flood_sender_queued
        && flood_status_b.failure
            == Horse::RollbackUdpWorkerFailure::QueueOverflow
        && flood_status_b.queue_overflows == 1
        && !flood_status_b.peer_ready;
    flood_worker_a.stop();
    flood_worker_b.stop();

    // Terminal reliability traffic can outlive Gekko polling. A service-side
    // drain must keep more than one full inbound-queue capacity of valid
    // control packets flowing without weakening the undrained overflow test
    // above.
    Horse::RollbackProductionConfig terminal_drain_a = a;
    terminal_drain_a.bind_port = 65454;
    terminal_drain_a.peer_port = 65455;
    Horse::RollbackProductionConfig terminal_drain_b = b;
    terminal_drain_b.bind_port = terminal_drain_a.peer_port;
    terminal_drain_b.peer_port = terminal_drain_a.bind_port;
    Horse::RollbackUdpNetworkWorker terminal_drain_worker_a {};
    Horse::RollbackUdpNetworkWorker terminal_drain_worker_b {};
    const bool terminal_drain_started =
        terminal_drain_worker_a.start(terminal_drain_a)
        && terminal_drain_worker_b.start(terminal_drain_b);
    const bool terminal_drain_ready = terminal_drain_started
        && wait_ready(
            terminal_drain_worker_a, terminal_drain_worker_b, 2000);
    uint32_t terminal_drain_sent = 0;
    uint32_t terminal_drain_received = 0;
    Horse::RollbackUdpMessage terminal_message {};
    for (uint32_t value = 0; terminal_drain_ready && value < 400; ++value)
    {
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(2);
        bool queued = false;
        while (!queued && std::chrono::steady_clock::now() < deadline)
        {
            queued = terminal_drain_worker_a.enqueue_redundant(
                Horse::RollbackProtocolV2PacketType::RoundTransition,
                &value,
                sizeof(value));
            while (terminal_drain_worker_b.dequeue(terminal_message))
                ++terminal_drain_received;
            if (!queued) std::this_thread::yield();
        }
        if (!queued) break;
        ++terminal_drain_sent;
    }
    const auto terminal_drain_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (terminal_drain_received < terminal_drain_sent
        && std::chrono::steady_clock::now() < terminal_drain_deadline)
    {
        while (terminal_drain_worker_b.dequeue(terminal_message))
            ++terminal_drain_received;
        std::this_thread::yield();
    }
    const auto terminal_drain_status_a = terminal_drain_worker_a.status();
    const auto terminal_drain_status_b = terminal_drain_worker_b.status();
    const bool terminal_drain_prevents_overflow = terminal_drain_ready
        && terminal_drain_sent == 400
        && terminal_drain_received == terminal_drain_sent
        && terminal_drain_status_a.failure
            == Horse::RollbackUdpWorkerFailure::None
        && terminal_drain_status_b.failure
            == Horse::RollbackUdpWorkerFailure::None
        && terminal_drain_status_a.queue_overflows == 0
        && terminal_drain_status_b.queue_overflows == 0;
    terminal_drain_worker_a.stop();
    terminal_drain_worker_b.stop();

    Horse::RollbackProductionConfig zero_delay = a;
    zero_delay.input_delay = 0;
    Horse::RollbackUdpNetworkWorker zero_delay_worker {};
    const bool zero_delay_rejected = !zero_delay.valid()
        && !zero_delay_worker.start(zero_delay)
        && zero_delay_worker.status().failure
            == Horse::RollbackUdpWorkerFailure::InvalidConfig;

    // A bounded producer queue must fail closed instead of silently dropping
    // authenticated Gekko or confirmation traffic. Exercise the exact worker
    // failure latch without a consumer thread draining the queue.
    Horse::RollbackUdpNetworkWorker overflow_worker {};
    bool overflow_prefix_accepted = true;
    for (uint32_t i = 0; i < 255; ++i)
    {
        overflow_prefix_accepted = overflow_prefix_accepted
            && overflow_worker.enqueue(
                Horse::RollbackProtocolV2PacketType::Gekko,
                &i,
                sizeof(i));
    }
    const uint32_t overflow_payload = 0xFFFFFFFFu;
    const bool overflow_rejected = !overflow_worker.enqueue(
        Horse::RollbackProtocolV2PacketType::Gekko,
        &overflow_payload,
        sizeof(overflow_payload));
    const auto overflow_status = overflow_worker.status();
    const bool overflow_failed_closed = overflow_prefix_accepted
        && overflow_rejected
        && overflow_status.failure
            == Horse::RollbackUdpWorkerFailure::QueueOverflow
        && overflow_status.queue_overflows == 1;
    overflow_worker.stop();

    // Periodic idempotent control retransmits use an independent small lane.
    // Saturating it must defer that retransmit without poisoning the primary
    // gameplay queue or stopping the session.
    Horse::RollbackUdpNetworkWorker redundant_worker {};
    bool redundant_prefix_accepted = true;
    for (uint32_t i = 0; i < 7; ++i)
    {
        redundant_prefix_accepted = redundant_prefix_accepted
            && redundant_worker.enqueue_redundant(
                Horse::RollbackProtocolV2PacketType::RoundTransition,
                &i,
                sizeof(i));
    }
    const bool redundant_deferred =
        !redundant_worker.enqueue_redundant(
            Horse::RollbackProtocolV2PacketType::RoundTransition,
            &overflow_payload,
            sizeof(overflow_payload));
    const bool primary_after_redundant_pressure =
        redundant_worker.enqueue(
            Horse::RollbackProtocolV2PacketType::Gekko,
            &overflow_payload,
            sizeof(overflow_payload));
    const auto redundant_status = redundant_worker.status();
    const bool redundant_pressure_nonfatal = redundant_prefix_accepted
        && redundant_deferred && primary_after_redundant_pressure
        && redundant_status.failure
            == Horse::RollbackUdpWorkerFailure::None
        && redundant_status.queue_overflows == 0
        && redundant_status.redundant_enqueue_deferrals == 1;
    redundant_worker.stop();

    const bool ok = config_equivalence && session_contract_hash
        && started && ready && queued && delivered
        && heartbeat_expired && cleanup
        && mismatch_started && mismatch_rejected
        && mismatch_stayed_unready && mismatch_cleanup
        && fixture_mismatch_rejected
        && replay_input_mismatch_rejected
        && network_profile_mismatch_rejected
        && wifi_started && wifi_ready && wifi_queued
        && wifi_fault_path_observed
        && stall_started && stall_telemetry && invalid_stall_configs
        && replay_started && replay_ready
        && replay_hash_mismatch_rejected
        && replay_anchor_mismatch_rejected
        && replay_nonce_mismatch_rejected
        && replay_domain_mismatch_rejected
        && replay_build_mismatch_rejected
        && replay_schema_mismatch_rejected
        && wrong_native_source_rejected
        && invalid_peer_address_rejected
        && dns_peer_resolved
        && flood_started && flood_ready && inbound_overflow_failed_closed
        && terminal_drain_prevents_overflow
        && zero_delay_rejected
        && overflow_failed_closed
        && redundant_pressure_nonfatal
        && status_ready_a.endpoint_pinned && status_b.endpoint_pinned
        && status_ready_a.handshake_generation == 1
        && status_b.handshake_generation == 1
        && status_a.packets_authenticated > 0
        && status_b.packets_authenticated > 0
        && status_a.queue_overflows == 0
        && status_b.queue_overflows == 0;

    std::printf(
        "rollback udp-runtime self-test %s config_equivalence=%d "
        "contract_hash=%d "
        "started=%d ready=%d queued=%d "
        "delivered=%d heartbeat_expired=%d cleanup=%d auth_a=%llu "
        "auth_b=%llu reject_a=%llu "
        "reject_b=%llu profile_started=%d profile_rejected=%d "
        "profile_stayed_unready=%d profile_cleanup=%d "
        "profile_reject_a=%llu profile_reject_b=%llu "
        "fixture_reject=%d replay_input_reject=%d "
        "network_profile_reject=%d "
        "wifi_ready=%d wifi_received=%u wifi_queued_packets=%llu "
        "stall_ready=%d stall_started_count=%llu "
        "stall_completed_count=%llu stall_actual_ms=%llu "
        "replay_ready=%d replay_hash_reject=%d replay_anchor_reject=%d "
        "replay_nonce_reject=%d replay_domain_reject=%d "
        "replay_build_reject=%d replay_schema_reject=%d "
        "native_source_reject=%d dns_peer=%d zero_delay=%d "
        "outbound_overflow=%d inbound_overflow=%d "
        "terminal_drain=%d redundant_deferral=%d\n",
        ok ? "passed" : "failed",
        config_equivalence ? 1 : 0,
        session_contract_hash ? 1 : 0,
        started ? 1 : 0,
        ready ? 1 : 0,
        queued ? 1 : 0,
        delivered ? 1 : 0,
        heartbeat_expired ? 1 : 0,
        cleanup ? 1 : 0,
        static_cast<unsigned long long>(status_a.packets_authenticated),
        static_cast<unsigned long long>(status_b.packets_authenticated),
        static_cast<unsigned long long>(status_a.packets_rejected),
        static_cast<unsigned long long>(status_b.packets_rejected),
        mismatch_started ? 1 : 0,
        mismatch_rejected ? 1 : 0,
        mismatch_stayed_unready ? 1 : 0,
        mismatch_cleanup ? 1 : 0,
        static_cast<unsigned long long>(
            mismatch_status_a.packets_rejected),
        static_cast<unsigned long long>(
            mismatch_status_b.packets_rejected),
        fixture_mismatch_rejected ? 1 : 0,
        replay_input_mismatch_rejected ? 1 : 0,
        network_profile_mismatch_rejected ? 1 : 0,
        wifi_ready ? 1 : 0,
        wifi_received,
        static_cast<unsigned long long>(wifi_status_a.fault_packets_queued),
        stall_ready ? 1 : 0,
        static_cast<unsigned long long>(
            stall_status_a.test_worker_stalls_started),
        static_cast<unsigned long long>(
            stall_status_a.test_worker_stalls_completed),
        static_cast<unsigned long long>(
            stall_status_a.test_worker_stall_actual_ms),
        replay_ready ? 1 : 0,
        replay_hash_mismatch_rejected ? 1 : 0,
        replay_anchor_mismatch_rejected ? 1 : 0,
        replay_nonce_mismatch_rejected ? 1 : 0,
        replay_domain_mismatch_rejected ? 1 : 0,
        replay_build_mismatch_rejected ? 1 : 0,
        replay_schema_mismatch_rejected ? 1 : 0,
        wrong_native_source_rejected ? 1 : 0,
        dns_peer_resolved ? 1 : 0,
        zero_delay_rejected ? 1 : 0,
        overflow_failed_closed ? 1 : 0,
        inbound_overflow_failed_closed ? 1 : 0,
        terminal_drain_prevents_overflow ? 1 : 0,
        redundant_pressure_nonfatal ? 1 : 0);
    return ok ? 0 : 1;
}
