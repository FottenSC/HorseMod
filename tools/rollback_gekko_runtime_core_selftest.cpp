#include "../HorseMod/horselib/RollbackGekkoRuntimeCore.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
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
        std::array<uint32_t, 128> snapshots {};
        uint32_t state {0};
        uint32_t saves {0};
        uint32_t loads {0};
        uint32_t advances {0};
        uint32_t rollback_advances {0};
        uint32_t sends {0};
        uint32_t receives {0};
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
            endpoint.state = endpoint.state * 1664525u + 1013904223u;
            endpoint.state ^= input[0] + (input[1] << 16u);
            ++endpoint.advances;
            if (event.data.adv.rolling_back)
                ++endpoint.rollback_advances;
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
        core_a.poll();
        core_b.poll();
    }
    const bool started = core_a.session_started() && core_b.session_started();
    bool drained = true;
    for (uint32_t i = 0; i < 100; ++i)
    {
        drained = core_a.poll() && core_b.poll()
            && core_a.drain(nullptr) && core_b.drain(nullptr)
            && drained;
    }
    const bool characterized = created && started && drained
        && !a.failure && !b.failure
        && a.sends && b.sends && a.receives && b.receives
        && a.saves && b.saves && (a.loads + b.loads) > 0
        && a.advances && b.advances
        && (a.rollback_advances + b.rollback_advances) > 0
        && a.state == b.state
        && core_a.desync_events() == 0 && core_b.desync_events() == 0
        && core_a.disconnect_events() == 0
        && core_b.disconnect_events() == 0;
    std::printf(
        "rollback gekko-runtime-core self-test %s started=%d "
        "saves=%u/%u loads=%u/%u advances=%u/%u rollback=%u/%u "
        "packets=%u/%u state=0x%08X/0x%08X failure=%s/%s\n",
        characterized ? "passed" : "failed", started ? 1 : 0,
        a.saves, b.saves, a.loads, b.loads, a.advances, b.advances,
        a.rollback_advances, b.rollback_advances, a.sends, b.sends,
        a.state, b.state, a.failure ? a.failure : "none",
        b.failure ? b.failure : "none");
    core_a.shutdown();
    core_b.shutdown();
    return characterized ? 0 : 1;
}
