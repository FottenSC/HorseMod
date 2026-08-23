#include "ReplayTracePlaybackGate.hpp"

#include <cstdio>

namespace
{
    bool require(bool condition, const char* message)
    {
        if (condition)
            return true;
        std::fprintf(stderr, "replay trace playback-gate self-test: %s\n",
                     message);
        return false;
    }
}

int main()
{
    Horse::ReplayTracePlaybackGate gate;
    bool ok = true;
    constexpr uintptr_t replay_a = 0x1000;
    constexpr uintptr_t replay_b = 0x2000;

    auto transition = gate.update(false, true, replay_a, 1);
    ok &= require(!transition.activate && !gate.active(),
                  "disabled configuration activated tracing");

    transition = gate.update(true, false, replay_a, 1);
    ok &= require(!transition.activate && !gate.active(),
                  "non-replay presence activated tracing");

    transition = gate.update(true, true, replay_a, 0);
    ok &= require(!transition.activate && !gate.active(),
                  "replay browser activated tracing");

    transition = gate.update(true, true, replay_a, 1);
    ok &= require(transition.activate && gate.active(),
                  "playback did not activate tracing");

    transition = gate.update(true, true, replay_a, -1);
    ok &= require(!transition.deactivate && gate.active(),
                  "temporarily unresolved playback stopped tracing");

    for (uint32_t tick = 0;
         tick < Horse::ReplayTracePlaybackGate::kPlaybackFalseGraceTicks;
         ++tick)
    {
        transition = gate.update(true, true, replay_a, 0);
        ok &= require(!transition.deactivate && gate.active(),
                      "transient false playback stopped tracing");
    }
    transition = gate.update(true, true, replay_a, 1);
    ok &= require(gate.active() && gate.false_ticks() == 0,
                  "playback recovery did not clear the debounce");

    transition = gate.update(true, true, replay_b, 1);
    ok &= require(transition.deactivate && transition.activate
                      && gate.active()
                      && gate.replay_player() == replay_b,
                  "replay-player replacement did not rotate the session");

    transition = gate.update(true, false, replay_b, 1);
    ok &= require(transition.deactivate && !gate.active(),
                  "leaving Replay presence did not stop tracing");
    transition = gate.update(true, true, replay_b, 1);
    ok &= require(transition.activate && gate.active(),
                  "re-entry did not start tracing");

    transition = gate.update(false, true, replay_b, 1);
    ok &= require(transition.deactivate && !gate.active(),
                  "disabling configuration did not stop tracing");
    transition = gate.update(true, true, replay_b, 1);
    ok &= require(transition.activate && gate.active(),
                  "configuration re-enable did not reactivate playback");
    ok &= require(gate.reset() && !gate.active(),
                  "shutdown reset did not stop tracing");

    transition = gate.update(true, true, replay_a, 1);
    ok &= require(transition.activate && gate.active(),
                  "final playback setup failed");
    for (uint32_t tick = 0;
         tick <= Horse::ReplayTracePlaybackGate::kPlaybackFalseGraceTicks;
         ++tick)
    {
        transition = gate.update(true, true, replay_a, 0);
    }
    ok &= require(transition.deactivate && !gate.active(),
                  "confirmed playback end did not stop tracing");

    if (!ok)
        return 1;
    std::puts("replay trace playback-gate self-test passed");
    return 0;
}
