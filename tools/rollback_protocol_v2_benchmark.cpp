#include "../HorseMod/horselib/RollbackProtocolV2.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    using namespace Horse;
    constexpr uint32_t kIterations = 10000;
    constexpr double kFrameBudgetMicroseconds = 16666.667;
    constexpr double kAuthenticationBudgetFraction = 0.01;
    constexpr double kRoundTripBudgetMicroseconds =
        kFrameBudgetMicroseconds * kAuthenticationBudgetFraction;

    RollbackProtocolV2Header header {};
    header.packet_type = RollbackProtocolV2PacketType::Input;
    header.source_peer = 1;
    header.destination_peer = 2;
    header.build_id = 0x1020304050607080ull;
    header.schema_id = 0x8877665544332211ull;
    header.sequence = 1;
    header.source_nonce.fill(0x5a);
    header.destination_nonce.fill(0xa5);
    std::array<uint8_t, 64> payload {};
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<uint8_t>(i);

    RollbackProtocolV2WirePacket wire {};
    RollbackProtocolV2Packet decoded {};
    const auto started = std::chrono::steady_clock::now();
    bool ok = true;
    for (uint32_t i = 0; i < kIterations; ++i)
    {
        header.sequence = i + 1;
        ok = EncodeRollbackProtocolV2Packet(
                header,
                payload.data(),
                static_cast<uint16_t>(payload.size()),
                "rollback-benchmark-secret",
                wire)
            && DecodeRollbackProtocolV2Packet(
                wire.bytes.data(),
                wire.size,
                "rollback-benchmark-secret",
                header.build_id,
                header.schema_id,
                decoded)
                .ok;
        if (!ok)
            break;
    }
    const auto stopped = std::chrono::steady_clock::now();
    const double elapsed_microseconds =
        std::chrono::duration<double, std::micro>(
            stopped - started)
            .count();
    const double round_trip_microseconds =
        elapsed_microseconds / static_cast<double>(kIterations);
    const double packets_per_second =
        static_cast<double>(kIterations * 2)
        * 1000000.0 / elapsed_microseconds;
    const bool budget_ok =
        round_trip_microseconds <= kRoundTripBudgetMicroseconds;
    std::atomic<bool> concurrent_ok {true};
    std::vector<std::thread> workers;
    for (uint32_t worker = 0; worker < 4; ++worker)
    {
        workers.emplace_back([worker, &concurrent_ok, payload]() {
            RollbackProtocolV2Header concurrent_header {};
            concurrent_header.packet_type =
                RollbackProtocolV2PacketType::Input;
            concurrent_header.source_peer = 1;
            concurrent_header.destination_peer = 2;
            concurrent_header.build_id = 0x1020304050607080ull;
            concurrent_header.schema_id = 0x8877665544332211ull;
            concurrent_header.source_nonce.fill(
                static_cast<uint8_t>(0x30 + worker));
            concurrent_header.destination_nonce.fill(
                static_cast<uint8_t>(0x60 + worker));
            for (uint64_t i = 0; i < 1000; ++i)
            {
                concurrent_header.sequence =
                    static_cast<uint64_t>(worker) * 1000 + i + 1;
                RollbackProtocolV2WirePacket concurrent_wire {};
                RollbackProtocolV2Packet concurrent_decoded {};
                if (!EncodeRollbackProtocolV2Packet(
                        concurrent_header,
                        payload.data(),
                        static_cast<uint16_t>(payload.size()),
                        "rollback-benchmark-secret",
                        concurrent_wire)
                    || !DecodeRollbackProtocolV2Packet(
                        concurrent_wire.bytes.data(),
                        concurrent_wire.size,
                        "rollback-benchmark-secret",
                        concurrent_header.build_id,
                        concurrent_header.schema_id,
                        concurrent_decoded)
                            .ok)
                {
                    concurrent_ok.store(false, std::memory_order_release);
                    return;
                }
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    const auto cache_stats = GetRollbackProtocolV2HmacCacheStats();
    const bool cache_ok =
        cache_stats.provider_initializations == 1
        && cache_stats.property_queries == 2
        && cache_stats.workspace_growths <= 10;

    std::printf(
        "rollback protocol-v2 benchmark %s iterations=%u "
        "encode_decode_us=%.3f packets_per_second=%.1f "
        "budget_us=%.3f budget_fraction=%.3f concurrent=%d "
        "provider_initializations=%llu property_queries=%llu "
        "workspace_growths=%llu\n",
        ok && budget_ok && concurrent_ok.load(std::memory_order_acquire)
                && cache_ok
            ? "passed"
            : "failed",
        kIterations,
        round_trip_microseconds,
        packets_per_second,
        kRoundTripBudgetMicroseconds,
        kAuthenticationBudgetFraction,
        concurrent_ok.load(std::memory_order_acquire) ? 1 : 0,
        static_cast<unsigned long long>(
            cache_stats.provider_initializations),
        static_cast<unsigned long long>(cache_stats.property_queries),
        static_cast<unsigned long long>(cache_stats.workspace_growths));
    return ok && budget_ok
            && concurrent_ok.load(std::memory_order_acquire)
            && cache_ok
        ? 0
        : 1;
}
