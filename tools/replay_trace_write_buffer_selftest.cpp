#include "ReplayTraceWriteBuffer.hpp"

#include <cstdio>
#include <string>

namespace
{
    bool require(bool condition, const char* message)
    {
        if (condition)
            return true;
        std::fprintf(stderr, "replay trace write-buffer self-test: %s\n", message);
        return false;
    }
}

int main()
{
    Horse::ReplayTraceWriteBuffer buffer;
    bool ok = true;
    ok &= require(buffer.empty(), "new buffer is not empty");

    const std::string prefix = "{\"event\":\"first\"}\n";
    buffer.append(prefix);
    ok &= require(buffer.view() == prefix, "first record changed");
    ok &= require(!buffer.should_flush(), "small record flushed early");
    ok &= require(buffer.should_flush(100, 0, 1000),
                  "first record was not made visible");
    ok &= require(!buffer.should_flush(200, 100, 1000),
                  "small record flushed before live-reader deadline");
    ok &= require(buffer.should_flush(350, 100, 1000),
                  "live-reader deadline did not request flush");

    const size_t remaining =
        Horse::ReplayTraceWriteBuffer::kFlushThresholdBytes - buffer.size();
    buffer.append(std::string(remaining - 1, 'x'));
    ok &= require(!buffer.should_flush(), "buffer flushed below threshold");
    buffer.append("y");
    ok &= require(buffer.should_flush(), "threshold did not request flush");
    ok &= require(buffer.view().substr(0, prefix.size()) == prefix,
                  "record ordering changed while batching");
    ok &= require(buffer.view().back() == 'y', "last byte was lost");

    buffer.clear();
    ok &= require(buffer.empty(), "clear retained buffered bytes");
    ok &= require(!buffer.should_flush(1000, 0, 1000),
                  "empty buffer requested a flush");
    buffer.append("one\n");
    buffer.append("two\n");
    ok &= require(buffer.view() == "one\ntwo\n",
                  "records were not retained in append order");

    if (!ok)
        return 1;
    std::puts("replay trace write-buffer self-test passed");
    return 0;
}
