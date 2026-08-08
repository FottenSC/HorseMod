#include "../HorseMod/horselib/RollbackGekkoRuntimeCore.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    struct Endpoint
    {
        uint8_t peer {0};
        Endpoint* remote {nullptr};
        std::deque<std::vector<uint8_t>> inbox;
        std::deque<std::vector<uint8_t>> delayed;
        bool delay_outbound {false};
        bool fatal_receive {false};
        std::array<uint32_t, 128> snapshots {};
        uint32_t state {0};
        uint32_t saves {0};
        uint32_t loads {0};
        uint32_t advances {0};
        uint32_t ordinary_advances {0};
        uint32_t rollback_advances {0};
        uint32_t runahead_advances {0};
        uint32_t last_ordinary_frame {0};
        int32_t last_load_frame {-2};
        uint32_t sends {0};
        uint32_t receives {0};
        std::array<std::array<uint32_t, 2>, 128> frame_inputs {};
        std::array<uint32_t, 128> frame_input_observations {};
        uint32_t game_events {0};
        const char* failure {nullptr};
    };

    bool send_packet(void* opaque, uint8_t remote_peer,
                     const void* data, uint16_t bytes) noexcept
    {
        auto& endpoint = *static_cast<Endpoint*>(opaque);
        if (!endpoint.remote || endpoint.remote->peer != remote_peer
            || !data || bytes == 0)
            return false;
        const auto* first = static_cast<const uint8_t*>(data);
        if (endpoint.delay_outbound)
            endpoint.delayed.emplace_back(first, first + bytes);
        else
            endpoint.remote->inbox.emplace_back(first, first + bytes);
        ++endpoint.sends;
        return true;
    }

    Horse::RollbackGekkoReceiveStatus receive_packet(
        void* opaque, Horse::RollbackGekkoDatagram& out) noexcept
    {
        auto& endpoint = *static_cast<Endpoint*>(opaque);
        if (endpoint.fatal_receive)
            return Horse::RollbackGekkoReceiveStatus::Fatal;
        if (endpoint.inbox.empty())
            return Horse::RollbackGekkoReceiveStatus::Empty;
        std::vector<uint8_t> bytes = std::move(endpoint.inbox.front());
        endpoint.inbox.pop_front();
        if (bytes.empty() || bytes.size() > out.payload.size())
            return Horse::RollbackGekkoReceiveStatus::Fatal;
        out.remote_peer = endpoint.remote->peer;
        out.bytes = static_cast<uint16_t>(bytes.size());
        std::memcpy(out.payload.data(), bytes.data(), bytes.size());
        ++endpoint.receives;
        return Horse::RollbackGekkoReceiveStatus::Packet;
    }

    bool game_event(void* opaque, GekkoGameEvent& event,
                    const void*) noexcept
    {
        auto& endpoint = *static_cast<Endpoint*>(opaque);
        ++endpoint.game_events;
        if (event.type == GekkoSaveEvent)
        {
            const uint32_t frame = static_cast<uint32_t>(event.data.save.frame);
            endpoint.snapshots[frame & 127u] = endpoint.state;
            std::memcpy(event.data.save.state, &endpoint.state,
                        sizeof(endpoint.state));
            *event.data.save.state_len = sizeof(endpoint.state);
            *event.data.save.checksum = endpoint.state;
            ++endpoint.saves;
            return true;
        }
        if (event.type == GekkoLoadEvent)
        {
            if (event.data.load.state_len != sizeof(endpoint.state))
                return false;
            std::memcpy(&endpoint.state, event.data.load.state,
                        sizeof(endpoint.state));
            endpoint.last_load_frame = event.data.load.frame;
            ++endpoint.loads;
            return true;
        }
        if (event.type == GekkoAdvanceEvent)
        {
            if (!event.data.adv.inputs
                || event.data.adv.input_len != 2 * sizeof(uint32_t))
                return false;
            uint32_t input[2] {};
            std::memcpy(input, event.data.adv.inputs, sizeof(input));
            if (event.data.adv.frame >= 0)
            {
                const size_t slot =
                    static_cast<uint32_t>(event.data.adv.frame) & 127u;
                endpoint.frame_inputs[slot] = {input[0], input[1]};
                ++endpoint.frame_input_observations[slot];
            }
            endpoint.state = endpoint.state * 1664525u + 1013904223u;
            endpoint.state ^= input[0] + (input[1] << 16u);
            ++endpoint.advances;
            if (event.data.adv.rolling_back)
                ++endpoint.rollback_advances;
            else if (event.data.adv.running_ahead)
                ++endpoint.runahead_advances;
            else
            {
                ++endpoint.ordinary_advances;
                endpoint.last_ordinary_frame =
                    static_cast<uint32_t>(event.data.adv.frame);
            }
            return true;
        }
        return false;
    }

    void failure(void* opaque, const char* reason) noexcept
    {
        static_cast<Endpoint*>(opaque)->failure = reason;
    }

    Horse::RollbackGekkoRuntimeCallbacks callbacks(Endpoint& endpoint)
    {
        Horse::RollbackGekkoRuntimeCallbacks out {};
        out.context = &endpoint;
        out.send = &send_packet;
        out.receive = &receive_packet;
        out.game_event = &game_event;
        out.failure = &failure;
        return out;
    }
}

int main()
{
    std::fprintf(stderr, "gekko-runtime-core phase=begin\n");
    std::fflush(stderr);
    GekkoGameEvent clock_event {};
    clock_event.type = GekkoAdvanceEvent;
    clock_event.data.adv.frame = 120;
    uint32_t characterized_clock =
        Horse::RollbackGekkoNextInputFrameAfterEvent(0, clock_event);
    clock_event.data.adv.frame = 90;
    clock_event.data.adv.rolling_back = true;
    characterized_clock = Horse::RollbackGekkoNextInputFrameAfterEvent(
        characterized_clock, clock_event);
    const bool rollback_did_not_move_clock = characterized_clock == 121;
    clock_event.data.adv.rolling_back = false;
    clock_event.data.adv.running_ahead = true;
    clock_event.data.adv.frame = 126;
    characterized_clock = Horse::RollbackGekkoNextInputFrameAfterEvent(
        characterized_clock, clock_event);
    const bool runahead_did_not_move_clock = characterized_clock == 121;
    const bool pregame_prefix_gate_sequence =
        Horse::RollbackGekkoPreGameplayPollRequired(false, true, false)
        && Horse::RollbackGekkoPreGameplayPollRequired(true, true, false)
        && Horse::RollbackGekkoPreGameplayFrameZeroBlocked(true, false)
        && !Horse::RollbackGekkoPreGameplayPollRequired(true, true, true)
        && !Horse::RollbackGekkoPreGameplayFrameZeroBlocked(true, true)
        && Horse::RollbackGekkoPreGameplayPollRequired(false, false, true)
        && !Horse::RollbackGekkoPreGameplayFrameZeroBlocked(false, false);

    Endpoint a {1};
    Endpoint b {2};
    a.remote = &b;
    b.remote = &a;
    Horse::RollbackGekkoRuntimeCore core_a {};
    Horse::RollbackGekkoRuntimeCore core_b {};
    Horse::RollbackGekkoRuntimeConfig config_a {};
    config_a.local_player_slot = 0;
    config_a.remote_peer = 2;
    config_a.rollback_window = 60;
    config_a.input_delay = 1;
    config_a.state_size = sizeof(uint32_t);
    Horse::RollbackGekkoRuntimeConfig config_b = config_a;
    config_b.local_player_slot = 1;
    config_b.remote_peer = 1;

    const bool created = core_a.start(config_a, callbacks(a))
        && core_b.start(config_b, callbacks(b));
    bool frame_clock_ok = created
        && core_a.next_input_frame() == 0
        && core_b.next_input_frame() == 0;
    for (uint32_t frame = 0; created && frame < 48; ++frame)
    {
        if (frame == 8) b.delay_outbound = true;
        if (frame == 20)
        {
            b.delay_outbound = false;
            while (!b.delayed.empty())
            {
                a.inbox.push_back(std::move(b.delayed.front()));
                b.delayed.pop_front();
            }
        }
        const uint32_t input_a = (frame * 3u) & 0x3FFFu;
        const uint32_t input_b = (frame * 7u + 1u) & 0x3FFFu;
        if (!core_a.update(input_a, nullptr)
            || !core_b.update(input_b, nullptr))
            break;
        frame_clock_ok = frame_clock_ok
            && core_a.next_input_frame()
                == (a.ordinary_advances ? a.last_ordinary_frame + 1u : 0u)
            && core_b.next_input_frame()
                == (b.ordinary_advances ? b.last_ordinary_frame + 1u : 0u);
        core_a.poll();
        core_b.poll();
    }
    const bool started = core_a.session_started() && core_b.session_started();
    const uint32_t ordinary_a_before_flush = a.ordinary_advances;
    const uint32_t ordinary_b_before_flush = b.ordinary_advances;
    bool drained = true;
    for (uint32_t i = 0; i < 100; ++i)
    {
        drained = core_a.poll() && core_b.poll()
            && core_a.flush_terminal_corrections(nullptr)
            && core_b.flush_terminal_corrections(nullptr)
            && drained;
        frame_clock_ok = frame_clock_ok
            && core_a.next_input_frame()
                == (a.ordinary_advances ? a.last_ordinary_frame + 1u : 0u)
            && core_b.next_input_frame()
                == (b.ordinary_advances ? b.last_ordinary_frame + 1u : 0u);
    }
    const bool characterized = created && started && drained
        && !a.failure && !b.failure
        && a.sends && b.sends && a.receives && b.receives
        && a.saves && b.saves && (a.loads + b.loads) > 0
        && a.advances && b.advances
        && (a.rollback_advances + b.rollback_advances) > 0
        && rollback_did_not_move_clock && runahead_did_not_move_clock
        && a.ordinary_advances == ordinary_a_before_flush
        && b.ordinary_advances == ordinary_b_before_flush
        && core_a.correction_flush_calls() == 100
        && core_b.correction_flush_calls() == 100
        && frame_clock_ok
        && a.advances == a.ordinary_advances + a.rollback_advances
            + a.runahead_advances
        && b.advances == b.ordinary_advances + b.rollback_advances
            + b.runahead_advances
        && a.state == b.state
        && core_a.desync_events() == 0 && core_b.desync_events() == 0
        && core_a.disconnect_events() == 0
        && core_b.disconnect_events() == 0;

    // A game-thread pause longer than Gekko's stock five-second timeout must
    // not disconnect an otherwise owned Horse session. Production transport
    // liveness is observed independently by Horse's authenticated worker.
    const uint32_t stall_sends_a = a.sends;
    const uint32_t stall_sends_b = b.sends;
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));
    const bool external_disconnect_authority = core_a.poll()
        && core_b.poll() && core_a.poll()
        && !core_a.fatal() && !core_b.fatal()
        && core_a.disconnect_events() == 0
        && core_b.disconnect_events() == 0
        && a.sends > stall_sends_a && b.sends > stall_sends_b;
    std::fprintf(stderr,
        "gekko-runtime-core phase=ordinary ok=%d created=%d started=%d "
        "external-disconnect-authority=%d\n",
        characterized ? 1 : 0, created ? 1 : 0, started ? 1 : 0,
        external_disconnect_authority ? 1 : 0);
    std::fflush(stderr);

    // Reproduce the production fixture exactly: native slot 0 changes from
    // neutral to 0x0003 at frame 120 while all of its Gekko datagrams are held
    // for six ordinary updates, then released in original order. The remote
    // peer must Load the frame-120 snapshot and replay past the correction.
    Endpoint fixture_owner {3};
    Endpoint fixture_receiver {4};
    fixture_owner.remote = &fixture_receiver;
    fixture_receiver.remote = &fixture_owner;
    auto fixture_owner_core =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    auto fixture_receiver_core =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    Horse::RollbackGekkoRuntimeConfig fixture_owner_config = config_a;
    fixture_owner_config.remote_peer = fixture_receiver.peer;
    Horse::RollbackGekkoRuntimeConfig fixture_receiver_config = config_b;
    fixture_receiver_config.remote_peer = fixture_owner.peer;
    const bool fixture_created = fixture_owner_core->start(
            fixture_owner_config, callbacks(fixture_owner))
        && fixture_receiver_core->start(
            fixture_receiver_config, callbacks(fixture_receiver));
    uint32_t receiver_loads_before_correction = 0;
    bool fixture_released = false;
    for (uint32_t update = 0;
         fixture_created && update < 220
            && fixture_owner_core->next_input_frame() < 180;
         ++update)
    {
        const uint32_t owner_frame =
            fixture_owner_core->next_input_frame();
        if (owner_frame == 120)
        {
            receiver_loads_before_correction = fixture_receiver.loads;
            fixture_owner.delay_outbound = true;
        }
        const uint32_t owner_input = owner_frame >= 120
                && owner_frame < 126
            ? 0x0003u : 0u;
        if (!fixture_owner_core->update(owner_input, nullptr)
            || !fixture_receiver_core->update(0u, nullptr))
        {
            break;
        }
        (void)fixture_owner_core->poll();
        (void)fixture_receiver_core->poll();
        if (fixture_owner_core->next_input_frame() >= 132)
            break;
    }
    const uint32_t fixture_owner_ordinary_before_flush =
        fixture_owner.ordinary_advances;
    const uint32_t fixture_receiver_ordinary_before_flush =
        fixture_receiver.ordinary_advances;
    fixture_owner.delay_outbound = false;
    while (!fixture_owner.delayed.empty())
    {
        fixture_receiver.inbox.push_back(
            std::move(fixture_owner.delayed.front()));
        fixture_owner.delayed.pop_front();
    }
    fixture_released = true;
    for (uint32_t i = 0; fixture_created && i < 100; ++i)
    {
        (void)fixture_owner_core->poll();
        (void)fixture_receiver_core->poll();
        (void)fixture_owner_core->flush_terminal_corrections(nullptr);
        (void)fixture_receiver_core->flush_terminal_corrections(nullptr);
    }
    const bool fixture_characterized = fixture_created
        && fixture_released
        && fixture_owner.delayed.empty()
        && fixture_receiver.loads > receiver_loads_before_correction
        && fixture_receiver.last_load_frame == 120
        // Input delay one applies submission 120 at game frame 121. Releasing
        // the held terminal packets only after ordinary simulation stops must
        // rewind snapshot 120 and replay the already-predicted frames without
        // emitting another ordinary Advance.
        && fixture_receiver.rollback_advances >= 11
        && fixture_owner.ordinary_advances
            == fixture_owner_ordinary_before_flush
        && fixture_receiver.ordinary_advances
            == fixture_receiver_ordinary_before_flush
        && fixture_owner.state == fixture_receiver.state
        && !fixture_owner.failure && !fixture_receiver.failure;
    std::fprintf(stderr,
        "gekko-runtime-core phase=terminal-fixture ok=%d created=%d\n",
        fixture_characterized ? 1 : 0, fixture_created ? 1 : 0);
    std::fflush(stderr);

    // Exact replay alignment needs authored inputs in Gekko's delay prefix.
    // Priming must be a storage-only operation: no update, Save, or Advance
    // may occur before logical frame zero.
    Endpoint prefix_a {5};
    Endpoint prefix_b {6};
    prefix_a.remote = &prefix_b;
    prefix_b.remote = &prefix_a;
    auto prefix_core_a =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    auto prefix_core_b =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    Horse::RollbackGekkoRuntimeConfig prefix_config_a = config_a;
    prefix_config_a.remote_peer = prefix_b.peer;
    prefix_config_a.input_delay = 3;
    Horse::RollbackGekkoRuntimeConfig prefix_config_b = prefix_config_a;
    prefix_config_b.local_player_slot = 1;
    prefix_config_b.remote_peer = prefix_a.peer;
    const std::array<uint32_t, 3> authored_prefix_a {
        0x0011u, 0x0022u, 0x0033u,
    };
    const std::array<uint32_t, 3> authored_prefix_b {
        0x0101u, 0x0202u, 0x0303u,
    };
    const bool prefix_created = prefix_core_a->start(
            prefix_config_a, callbacks(prefix_a))
        && prefix_core_b->start(prefix_config_b, callbacks(prefix_b));
    const bool prefix_primed = prefix_created
        && prefix_core_a->prime_local_delay_prefix(
            authored_prefix_a.data(), authored_prefix_a.size())
        && prefix_core_b->prime_local_delay_prefix(
            authored_prefix_b.data(), authored_prefix_b.size());
    std::fprintf(stderr,
        "gekko-runtime-core phase=prefix-prime created=%d primed=%d\n",
        prefix_created ? 1 : 0, prefix_primed ? 1 : 0);
    std::fflush(stderr);
    const bool prefix_did_not_advance = prefix_primed
        && prefix_core_a->update_calls() == 0
        && prefix_core_b->update_calls() == 0
        && prefix_core_a->next_input_frame() == 0
        && prefix_core_b->next_input_frame() == 0
        && prefix_a.saves == 0 && prefix_b.saves == 0
        && prefix_a.advances == 0 && prefix_b.advances == 0
        && prefix_a.sends == 0 && prefix_b.sends == 0
        && prefix_a.receives == 0 && prefix_b.receives == 0
        && prefix_a.game_events == 0 && prefix_b.game_events == 0
        && !prefix_a.failure && !prefix_b.failure
        && prefix_core_a->delay_prefix_inputs() == 3
        && prefix_core_b->delay_prefix_inputs() == 3;
    const bool duplicate_prefix_rejected = prefix_primed
        && !prefix_core_a->prime_local_delay_prefix(
            authored_prefix_a.data(), authored_prefix_a.size());
    for (uint32_t poll = 0; prefix_primed && poll < 80
        && (!prefix_core_a->delay_prefix_ready()
            || !prefix_core_b->delay_prefix_ready()); ++poll)
    {
        (void)prefix_core_a->poll();
        (void)prefix_core_b->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    const bool bilateral_prefix_ready = prefix_primed
        && prefix_core_a->delay_prefix_ready()
        && prefix_core_b->delay_prefix_ready()
        && prefix_core_a->session_started()
        && prefix_core_b->session_started()
        && prefix_a.saves == 0 && prefix_b.saves == 0
        && prefix_a.advances == 0 && prefix_b.advances == 0
        && prefix_a.game_events == 0 && prefix_b.game_events == 0;
    for (uint32_t update = 0; prefix_primed && update < 20; ++update)
    {
        const uint32_t input_a = 0x1000u + update + 3u;
        const uint32_t input_b = 0x2000u + update + 3u;
        if (!prefix_core_a->update(input_a, nullptr)
            || !prefix_core_b->update(input_b, nullptr))
            break;
        (void)prefix_core_a->poll();
        (void)prefix_core_b->poll();
    }
    for (uint32_t poll = 0; prefix_primed && poll < 20; ++poll)
    {
        (void)prefix_core_a->poll();
        (void)prefix_core_b->poll();
        (void)prefix_core_a->flush_terminal_corrections(nullptr);
        (void)prefix_core_b->flush_terminal_corrections(nullptr);
    }
    bool authored_prefix_consumed = true;
    for (uint32_t frame = 0; frame < 3; ++frame)
    {
        authored_prefix_consumed = authored_prefix_consumed
            && prefix_a.frame_input_observations[frame] != 0
            && prefix_b.frame_input_observations[frame] != 0
            && prefix_a.frame_inputs[frame][0] == authored_prefix_a[frame]
            && prefix_a.frame_inputs[frame][1] == authored_prefix_b[frame]
            && prefix_b.frame_inputs[frame][0] == authored_prefix_a[frame]
            && prefix_b.frame_inputs[frame][1] == authored_prefix_b[frame];
    }
    const bool first_submission_consumed =
        prefix_a.frame_input_observations[3] != 0
        && prefix_b.frame_input_observations[3] != 0
        && prefix_a.frame_inputs[3][0] == 0x1003u
        && prefix_a.frame_inputs[3][1] == 0x2003u
        && prefix_b.frame_inputs[3][0] == 0x1003u
        && prefix_b.frame_inputs[3][1] == 0x2003u;

    Endpoint zero_delay_endpoint {7};
    auto zero_delay_core =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    Horse::RollbackGekkoRuntimeConfig zero_delay_config = config_a;
    zero_delay_config.remote_peer = 8;
    zero_delay_config.input_delay = 0;
    const bool zero_delay_created = zero_delay_core->start(
        zero_delay_config, callbacks(zero_delay_endpoint));
    const bool zero_delay_primed = zero_delay_created
        && zero_delay_core->prime_local_delay_prefix(nullptr, 0)
        && zero_delay_core->delay_prefix_inputs() == 0
        && !zero_delay_core->prime_local_delay_prefix(nullptr, 0)
        && zero_delay_endpoint.sends == 0
        && zero_delay_endpoint.receives == 0
        && zero_delay_endpoint.game_events == 0
        && !zero_delay_endpoint.failure;

    Endpoint late_prime_endpoint {9};
    Endpoint late_prime_remote {10};
    late_prime_endpoint.remote = &late_prime_remote;
    late_prime_remote.remote = &late_prime_endpoint;
    auto late_prime_core =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    Horse::RollbackGekkoRuntimeConfig late_prime_config = config_a;
    late_prime_config.remote_peer = 10;
    const std::array<uint32_t, 1> late_prefix {0x55u};
    const bool late_prime_created = late_prime_core->start(
        late_prime_config, callbacks(late_prime_endpoint));
    const bool late_prime_rejected = late_prime_created
        && late_prime_core->poll()
        && !late_prime_core->prime_local_delay_prefix(
            late_prefix.data(), late_prefix.size());
    Endpoint failed_poll_endpoint {11};
    Endpoint failed_poll_remote {12};
    failed_poll_endpoint.remote = &failed_poll_remote;
    failed_poll_remote.remote = &failed_poll_endpoint;
    auto failed_poll_core =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    Horse::RollbackGekkoRuntimeConfig failed_poll_config = config_a;
    failed_poll_config.remote_peer = failed_poll_remote.peer;
    const bool failed_poll_created = failed_poll_core->start(
        failed_poll_config, callbacks(failed_poll_endpoint));
    const std::array<uint32_t, 1> failed_poll_prefix {0x66u};
    const bool failed_poll_closed = failed_poll_created
        && failed_poll_core->prime_local_delay_prefix(
            failed_poll_prefix.data(), failed_poll_prefix.size());
    failed_poll_endpoint.fatal_receive = true;
    const bool failed_poll_rejected = failed_poll_closed
        && !failed_poll_core->poll()
        && failed_poll_core->fatal()
        && failed_poll_endpoint.failure != nullptr
        && failed_poll_endpoint.game_events == 0;
    const bool prefix_characterized = prefix_primed
        && prefix_did_not_advance && duplicate_prefix_rejected
        && bilateral_prefix_ready
        && authored_prefix_consumed && first_submission_consumed
        && zero_delay_primed && late_prime_rejected
        && pregame_prefix_gate_sequence && failed_poll_rejected
        && prefix_a.state == prefix_b.state
        && !prefix_a.failure && !prefix_b.failure;

    Endpoint forced_a {13};
    Endpoint forced_b {14};
    forced_a.remote = &forced_b;
    forced_b.remote = &forced_a;
    auto forced_core_a =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    auto forced_core_b =
        std::make_unique<Horse::RollbackGekkoRuntimeCore>();
    Horse::RollbackGekkoRuntimeConfig invalid_forced = config_a;
    invalid_forced.forced_rollback_depth = 6;
    const bool forced_requires_every_advance = !invalid_forced.valid();
    Horse::RollbackGekkoRuntimeConfig forced_config_a = invalid_forced;
    forced_config_a.save_policy = Horse::RollbackSavePolicy::EveryAdvance;
    forced_config_a.remote_peer = forced_b.peer;
    Horse::RollbackGekkoRuntimeConfig forced_config_b = forced_config_a;
    forced_config_b.local_player_slot = 1;
    forced_config_b.remote_peer = forced_a.peer;
    const bool forced_created = forced_core_a->start(
            forced_config_a, callbacks(forced_a))
        && forced_core_b->start(forced_config_b, callbacks(forced_b));
    int32_t confirmed_a = -1;
    int32_t confirmed_b = -1;
    bool confirmed_monotonic = true;
    for (uint32_t frame = 0; forced_created && frame < 40; ++frame)
    {
        if (!forced_core_a->update(frame & 3u, nullptr)
            || !forced_core_b->update((frame + 1u) & 3u, nullptr))
            break;
        (void)forced_core_a->poll();
        (void)forced_core_b->poll();
        const int32_t next_a = forced_core_a->confirmed_input_frame();
        const int32_t next_b = forced_core_b->confirmed_input_frame();
        confirmed_monotonic = confirmed_monotonic
            && (confirmed_a < 0 || next_a < 0 || next_a >= confirmed_a)
            && (confirmed_b < 0 || next_b < 0 || next_b >= confirmed_b);
        if (next_a >= 0) confirmed_a = next_a;
        if (next_b >= 0) confirmed_b = next_b;
    }
    for (uint32_t poll = 0; forced_created && poll < 20; ++poll)
    {
        (void)forced_core_a->poll();
        (void)forced_core_b->poll();
        (void)forced_core_a->flush_corrections(nullptr);
        (void)forced_core_b->flush_corrections(nullptr);
    }
    const bool forced_characterized = forced_requires_every_advance
        && forced_created && confirmed_monotonic
        && forced_core_a->forced_rollback_eligible_updates() > 0
        && forced_core_b->forced_rollback_eligible_updates() > 0
        && forced_core_a->forced_rollback_completed_updates()
            == forced_core_a->forced_rollback_eligible_updates()
        && forced_core_b->forced_rollback_completed_updates()
            == forced_core_b->forced_rollback_eligible_updates()
        && forced_a.loads > 0 && forced_b.loads > 0
        && forced_a.rollback_advances >= 6
        && forced_b.rollback_advances >= 6
        && forced_a.state == forced_b.state
        && !forced_a.failure && !forced_b.failure;
    std::fprintf(stderr,
        "gekko-runtime-core phase=prefix-result ok=%d consumed=%d "
        "ready=%d gate=%d poll-fail=%d first=%d zero=%d late=%d "
        "state=%d failure=%s/%s\n",
        prefix_characterized ? 1 : 0,
        authored_prefix_consumed ? 1 : 0,
        bilateral_prefix_ready ? 1 : 0,
        pregame_prefix_gate_sequence ? 1 : 0,
        failed_poll_rejected ? 1 : 0,
        first_submission_consumed ? 1 : 0,
        zero_delay_primed ? 1 : 0,
        late_prime_rejected ? 1 : 0,
        prefix_a.state == prefix_b.state ? 1 : 0,
        prefix_a.failure ? prefix_a.failure : "none",
        prefix_b.failure ? prefix_b.failure : "none");
    std::fflush(stderr);
    std::printf(
        "rollback gekko-runtime-core self-test %s started=%d "
        "saves=%u/%u loads=%u/%u advances=%u/%u rollback=%u/%u "
        "runahead=%u/%u "
        "packets=%u/%u input-frame=%u/%u state=0x%08X/0x%08X "
        "failure=%s/%s\n",
        characterized && external_disconnect_authority
            && fixture_characterized && prefix_characterized
            && forced_characterized
            ? "passed" : "failed",
        started ? 1 : 0,
        a.saves, b.saves, a.loads, b.loads, a.advances, b.advances,
        a.rollback_advances, b.rollback_advances,
        a.runahead_advances, b.runahead_advances, a.sends, b.sends,
        core_a.next_input_frame(), core_b.next_input_frame(),
        a.state, b.state, a.failure ? a.failure : "none",
        b.failure ? b.failure : "none");
    std::printf(
        "fixture hold characterized=%d released=%d loads=%u frame=%d "
        "rollback=%u state=0x%08X/0x%08X\n",
        fixture_characterized ? 1 : 0, fixture_released ? 1 : 0,
        fixture_receiver.loads, fixture_receiver.last_load_frame,
        fixture_receiver.rollback_advances, fixture_owner.state,
        fixture_receiver.state);
    std::printf(
        "authored delay prefix characterized=%d primed=%d no-advance=%d "
        "duplicate-rejected=%d ready=%d gate=%d poll-fail=%d consumed=%d "
        "first-submit=%d zero-delay=%d "
        "late-rejected=%d observations=%u/%u\n",
        prefix_characterized ? 1 : 0, prefix_primed ? 1 : 0,
        prefix_did_not_advance ? 1 : 0,
        duplicate_prefix_rejected ? 1 : 0,
        bilateral_prefix_ready ? 1 : 0,
        pregame_prefix_gate_sequence ? 1 : 0,
        failed_poll_rejected ? 1 : 0,
        authored_prefix_consumed ? 1 : 0,
        first_submission_consumed ? 1 : 0,
        zero_delay_primed ? 1 : 0,
        late_prime_rejected ? 1 : 0,
        prefix_a.frame_input_observations[0],
        prefix_b.frame_input_observations[0]);
    if (!characterized)
    {
        std::printf(
            "original checks created=%d started=%d drained=%d failure=%d/%d "
            "traffic=%d/%d/%d/%d events=%d/%d rollback=%d clocks=%d/%d/%d "
            "totals=%d/%d state=%d terminal=%d/%d/%d/%d\n",
            created ? 1 : 0, started ? 1 : 0, drained ? 1 : 0,
            a.failure ? 1 : 0, b.failure ? 1 : 0,
            a.sends ? 1 : 0, b.sends ? 1 : 0,
            a.receives ? 1 : 0, b.receives ? 1 : 0,
            a.saves ? 1 : 0, b.saves ? 1 : 0,
            (a.loads + b.loads) > 0 ? 1 : 0,
            rollback_did_not_move_clock ? 1 : 0,
            runahead_did_not_move_clock ? 1 : 0,
            frame_clock_ok ? 1 : 0,
            a.advances == a.ordinary_advances + a.rollback_advances
                + a.runahead_advances ? 1 : 0,
            b.advances == b.ordinary_advances + b.rollback_advances
                + b.runahead_advances ? 1 : 0,
            a.state == b.state ? 1 : 0,
            core_a.desync_events() == 0 ? 1 : 0,
            core_b.desync_events() == 0 ? 1 : 0,
            core_a.disconnect_events() == 0 ? 1 : 0,
            core_b.disconnect_events() == 0 ? 1 : 0);
    }
    core_a.shutdown();
    core_b.shutdown();
    fixture_owner_core->shutdown();
    fixture_receiver_core->shutdown();
    prefix_core_a->shutdown();
    prefix_core_b->shutdown();
    zero_delay_core->shutdown();
    late_prime_core->shutdown();
    failed_poll_core->shutdown();
    forced_core_a->shutdown();
    forced_core_b->shutdown();
    return characterized && external_disconnect_authority
        && fixture_characterized && prefix_characterized
        && forced_characterized
        ? 0 : 1;
}
