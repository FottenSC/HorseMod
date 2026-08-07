#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Horse
{
    // Replay tracing can emit hundreds of small JSON records per simulation
    // frame. Keep those records visible to the live reader while avoiding a
    // synchronous kernel write for every record.
    class ReplayTraceWriteBuffer
    {
    public:
        static constexpr size_t kFlushThresholdBytes = 64u * 1024u;
        static constexpr int64_t kMaximumBufferedIntervals = 4;

        void append(std::string_view bytes)
        {
            m_bytes.append(bytes.data(), bytes.size());
        }

        bool should_flush() const noexcept
        {
            return m_bytes.size() >= kFlushThresholdBytes;
        }

        bool should_flush(int64_t now, int64_t last_flush,
                          int64_t ticks_per_second) const noexcept
        {
            if (empty())
                return false;
            if (should_flush() || last_flush <= 0 || ticks_per_second <= 0)
                return true;
            return now - last_flush >=
                ticks_per_second / kMaximumBufferedIntervals;
        }

        bool empty() const noexcept { return m_bytes.empty(); }
        size_t size() const noexcept { return m_bytes.size(); }
        const char* data() const noexcept { return m_bytes.data(); }
        std::string_view view() const noexcept { return m_bytes; }
        void clear() noexcept { m_bytes.clear(); }

    private:
        std::string m_bytes;
    };
}
