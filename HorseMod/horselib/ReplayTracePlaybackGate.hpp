#pragma once

#include <cstdint>

namespace Horse
{
    class ReplayTracePlaybackGate
    {
    public:
        static constexpr uint32_t kPlaybackFalseGraceTicks = 30;

        struct Transition
        {
            bool activate {false};
            bool deactivate {false};
        };

        Transition update(bool configured,
                          bool in_replay_presence,
                          uintptr_t replay_player,
                          int32_t is_playing_back) noexcept
        {
            Transition out {};
            if (!configured || !in_replay_presence)
            {
                out.deactivate = reset();
                return out;
            }

            if (m_active && replay_player != 0 && m_replay_player != 0
                && replay_player != m_replay_player)
            {
                out.deactivate = reset();
            }

            if (!m_active)
            {
                if (replay_player != 0 && is_playing_back == 1)
                {
                    m_active = true;
                    m_replay_player = replay_player;
                    m_false_ticks = 0;
                    out.activate = true;
                }
                return out;
            }

            if (replay_player != 0 && m_replay_player == 0)
                m_replay_player = replay_player;

            if (is_playing_back == 1)
            {
                m_false_ticks = 0;
            }
            else if (is_playing_back == 0)
            {
                ++m_false_ticks;
                if (m_false_ticks > kPlaybackFalseGraceTicks)
                    out.deactivate = reset();
            }
            // An unresolved playback flag is deliberately ignored. Replay
            // actors can disappear briefly across round transitions.
            return out;
        }

        bool reset() noexcept
        {
            const bool was_active = m_active;
            m_active = false;
            m_replay_player = 0;
            m_false_ticks = 0;
            return was_active;
        }

        bool active() const noexcept { return m_active; }
        uintptr_t replay_player() const noexcept { return m_replay_player; }
        uint32_t false_ticks() const noexcept { return m_false_ticks; }

    private:
        bool m_active {false};
        uintptr_t m_replay_player {0};
        uint32_t m_false_ticks {0};
    };
}
