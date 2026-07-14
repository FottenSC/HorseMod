#include "../HorseMod/horselib/RollbackUdpRuntime.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
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
    Horse::RollbackProductionConfig changed_launch = a;
    ++changed_launch.launch_descriptor.stage;
    Horse::RollbackProductionConfig changed_profile = a;
    changed_profile.network_profile =
        Horse::RollbackNetworkProfileKind::Wifi50msJitter;
    Horse::RollbackProductionConfig changed_domain = a;
    changed_domain.session_domain =
        Horse::RollbackSessionDomain::ReplayForkLab;
    const bool config_equivalence =
        Horse::RollbackProductionConfigEquivalent(a, equivalent_a)
        && !Horse::RollbackProductionConfigEquivalent(a, b)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_secret)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_launch)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_profile)
        && !Horse::RollbackProductionConfigEquivalent(a, changed_domain);

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

    Horse::RollbackProductionConfig mirrored_a = a;
    mirrored_a.bind_port = 65426;
    mirrored_a.peer_port = 65427;
    mirrored_a.lifecycle_mode =
        Horse::RollbackLifecycleMode::MirroredVersus;
    mirrored_a.native_input_source_slot = 0;
    Horse::RollbackProductionConfig mirrored_b = b;
    mirrored_b.bind_port = mirrored_a.peer_port;
    mirrored_b.peer_port = mirrored_a.bind_port;
    mirrored_b.lifecycle_mode =
        Horse::RollbackLifecycleMode::MirroredVersus;
    mirrored_b.native_input_source_slot = 0;

    Horse::RollbackProductionConfig mode_mismatch_b = mirrored_b;
    mode_mismatch_b.lifecycle_mode =
        Horse::RollbackLifecycleMode::StockOnlinePvp;
    mode_mismatch_b.native_input_source_slot =
        mode_mismatch_b.local_player_slot;
    const bool mode_mismatch_rejected = profile_pair_rejected(
        mirrored_a, mode_mismatch_b);

    mirrored_a.bind_port = 65428;
    mirrored_a.peer_port = 65429;
    mirrored_b.bind_port = mirrored_a.peer_port;
    mirrored_b.peer_port = mirrored_a.bind_port;
    Horse::RollbackProductionConfig seed_mismatch_b = mirrored_b;
    ++seed_mismatch_b.launch_descriptor.seed;
    const bool seed_mismatch_rejected = profile_pair_rejected(
        mirrored_a, seed_mismatch_b);

    mirrored_a.bind_port = 65430;
    mirrored_a.peer_port = 65431;
    mirrored_b.bind_port = mirrored_a.peer_port;
    mirrored_b.peer_port = mirrored_a.bind_port;
    Horse::RollbackProductionConfig descriptor_mismatch_b = mirrored_b;
    ++descriptor_mismatch_b.launch_descriptor.stage;
    const bool descriptor_mismatch_rejected = profile_pair_rejected(
        mirrored_a, descriptor_mismatch_b);

    mirrored_a.bind_port = 65432;
    mirrored_a.peer_port = 65433;
    mirrored_b.bind_port = mirrored_a.peer_port;
    mirrored_b.peer_port = mirrored_a.bind_port;
    Horse::RollbackProductionConfig character_mismatch_b = mirrored_b;
    ++character_mismatch_b.launch_descriptor.right_character;
    const bool character_mismatch_rejected = profile_pair_rejected(
        mirrored_a, character_mismatch_b);

    mirrored_a.bind_port = 65434;
    mirrored_a.peer_port = 65435;
    mirrored_b.bind_port = mirrored_a.peer_port;
    mirrored_b.peer_port = mirrored_a.bind_port;
    mirrored_a.network_profile =
        Horse::RollbackNetworkProfileKind::Wifi50msJitter;
    mirrored_b.network_profile =
        Horse::RollbackNetworkProfileKind::Clean0ms;
    const bool network_profile_mismatch_rejected = profile_pair_rejected(
        mirrored_a, mirrored_b);

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

    Horse::RollbackProductionConfig wrong_native_source = mirrored_b;
    wrong_native_source.native_input_source_slot = 1;
    Horse::RollbackUdpNetworkWorker wrong_native_source_worker {};
    const bool wrong_native_source_rejected =
        !wrong_native_source.valid()
        && !wrong_native_source_worker.start(wrong_native_source)
        && wrong_native_source_worker.status().failure
            == Horse::RollbackUdpWorkerFailure::InvalidConfig;
    wrong_native_source_worker.stop();

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

    const bool ok = config_equivalence
        && started && ready && queued && delivered
        && heartbeat_expired && cleanup
        && mismatch_started && mismatch_rejected
        && mismatch_stayed_unready && mismatch_cleanup
        && mode_mismatch_rejected
        && seed_mismatch_rejected
        && descriptor_mismatch_rejected
        && character_mismatch_rejected
        && network_profile_mismatch_rejected
        && wifi_started && wifi_ready && wifi_queued
        && wifi_fault_path_observed
        && replay_started && replay_ready
        && replay_hash_mismatch_rejected
        && replay_anchor_mismatch_rejected
        && replay_nonce_mismatch_rejected
        && replay_domain_mismatch_rejected
        && replay_build_mismatch_rejected
        && replay_schema_mismatch_rejected
        && wrong_native_source_rejected
        && flood_started && flood_ready && inbound_overflow_failed_closed
        && zero_delay_rejected
        && overflow_failed_closed
        && status_ready_a.endpoint_pinned && status_b.endpoint_pinned
        && status_ready_a.handshake_generation == 1
        && status_b.handshake_generation == 1
        && status_a.packets_authenticated > 0
        && status_b.packets_authenticated > 0
        && status_a.queue_overflows == 0
        && status_b.queue_overflows == 0;

    std::printf(
        "rollback udp-runtime self-test %s config_equivalence=%d "
        "started=%d ready=%d queued=%d "
        "delivered=%d heartbeat_expired=%d cleanup=%d auth_a=%llu "
        "auth_b=%llu reject_a=%llu "
        "reject_b=%llu profile_started=%d profile_rejected=%d "
        "profile_stayed_unready=%d profile_cleanup=%d "
        "profile_reject_a=%llu profile_reject_b=%llu "
        "mode_reject=%d seed_reject=%d descriptor_reject=%d "
        "character_reject=%d network_profile_reject=%d "
        "wifi_ready=%d wifi_received=%u wifi_queued_packets=%llu "
        "replay_ready=%d replay_hash_reject=%d replay_anchor_reject=%d "
        "replay_nonce_reject=%d replay_domain_reject=%d "
        "replay_build_reject=%d replay_schema_reject=%d "
        "native_source_reject=%d zero_delay=%d "
        "outbound_overflow=%d inbound_overflow=%d\n",
        ok ? "passed" : "failed",
        config_equivalence ? 1 : 0,
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
        mode_mismatch_rejected ? 1 : 0,
        seed_mismatch_rejected ? 1 : 0,
        descriptor_mismatch_rejected ? 1 : 0,
        character_mismatch_rejected ? 1 : 0,
        network_profile_mismatch_rejected ? 1 : 0,
        wifi_ready ? 1 : 0,
        wifi_received,
        static_cast<unsigned long long>(wifi_status_a.fault_packets_queued),
        replay_ready ? 1 : 0,
        replay_hash_mismatch_rejected ? 1 : 0,
        replay_anchor_mismatch_rejected ? 1 : 0,
        replay_nonce_mismatch_rejected ? 1 : 0,
        replay_domain_mismatch_rejected ? 1 : 0,
        replay_build_mismatch_rejected ? 1 : 0,
        replay_schema_mismatch_rejected ? 1 : 0,
        wrong_native_source_rejected ? 1 : 0,
        zero_delay_rejected ? 1 : 0,
        overflow_failed_closed ? 1 : 0,
        inbound_overflow_failed_closed ? 1 : 0);
    return ok ? 0 : 1;
}
