// ============================================================================
// Horse::RollbackReplayInputScript
//
// Read-only replay .bin input-script extraction for live rollback tests.
// Replay files are treated as deterministic input streams only; this does not
// import replay state, launch replay mode, or mutate ReplayScrub.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"
#include "ReplayDebugTrace.hpp"
#include "RollbackConsumedInputSidecar.hpp"
#include "SafeMemoryRead.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace Horse
{
    // One process-wide diagnostic epoch shared by opcode-1 window writes,
    // opcode-0 exact writes, and the native cache-consumer observer. The high
    // bit is validity; opcode 1 is the only producer.
    inline std::atomic<uint64_t> g_rollbackReplayInputScriptEpoch {0};

    static inline void ResetRollbackReplayInputScriptEpoch() noexcept
    {
        g_rollbackReplayInputScriptEpoch.store(0, std::memory_order_release);
    }

    static inline bool EstablishRollbackReplayInputScriptEpoch(
        uint32_t frame) noexcept
    {
        const uint64_t encoded = (1ull << 63) | frame;
        uint64_t expected = 0;
        return g_rollbackReplayInputScriptEpoch.compare_exchange_strong(
            expected, encoded, std::memory_order_acq_rel,
            std::memory_order_acquire)
            || expected == encoded;
    }

    static inline bool GetRollbackReplayInputScriptEpoch(
        uint32_t& frame) noexcept
    {
        const uint64_t encoded = g_rollbackReplayInputScriptEpoch.load(
            std::memory_order_acquire);
        if ((encoded & (1ull << 63)) == 0) return false;
        frame = static_cast<uint32_t>(encoded);
        return true;
    }

    struct RollbackReplayInputScript
    {
        bool ok {false};
        bool wrapper_header {false};
        bool consumed_input_sidecar {false};
        bool payload_ulx1 {false};
        bool native_decompress_ok {false};
        bool native_setup_metadata_ok {false};
        uint64_t file_bytes {0};
        uint64_t file_hash {0};
        uint64_t payload_bytes {0};
        uint64_t payload_hash {0};
        uint64_t decompressed_bytes {0};
        uint64_t decompressed_hash {0};
        uint64_t input_blocks_detected {0};
        uint64_t input_block_pairs {0};
        uint64_t input_frames_p0 {0};
        uint64_t input_frames_p1 {0};
        uint64_t input_hash_p0 {0};
        uint64_t input_hash_p1 {0};
        int32_t setup_stage_index {-1};
        int32_t setup_left_chara_id {-1};
        int32_t setup_right_chara_id {-1};
        uint32_t setup_random_seed {0};
        uint64_t source_build_id {0};
        uint64_t source_schema_id {0};
        std::array<uint8_t, 32> source_replay_sha256 {};
        std::array<uint8_t, 32> source_oracle_sha256 {};
        std::array<uint8_t, 32> bound_artifact_sha256 {};
        const char* failure {"not-run"};
        std::array<std::vector<uint32_t>, 2> player_inputs {};
        std::vector<RollbackReplayInputRoundPair> round_pairs {};
    };

    static inline bool RollbackConsumedInputIdentityMatches(
        const RollbackReplayInputScript& script,
        uint64_t expected_build_id,
        uint64_t expected_schema_id) noexcept
    {
        return script.consumed_input_sidecar
            && expected_build_id != 0 && expected_schema_id != 0
            && script.source_build_id == expected_build_id
            && script.source_schema_id == expected_schema_id;
    }

    struct RollbackReplayInputInjectionReport
    {
        bool enabled {false};
        bool hooks_installed {false};
        bool script_available {false};
        bool send_cache_written {false};
        bool consumer_cache_written {false};
        bool injected {false};
        uint32_t local_player_slot {0};
        uint64_t script_frames {0};
        uint64_t send_windows {0};
        uint64_t send_cache_writes {0};
        uint64_t consumer_writes {0};
        uint64_t first_injected_frame {0};
        uint64_t last_injected_frame {0};
        uint64_t input_hash {0};
        uint64_t applied_input_hash {0};
        uint64_t applied_inputs {0};
        uint64_t cache_write_readback_hash {0};
        uint64_t cache_write_readbacks {0};
        bool cache_write_readback_ok {false};
        uint32_t script_epoch {0};
        bool script_epoch_set {false};
        const char* failure {"not-run"};
    };

    class RollbackReplayInputScriptExtractor
    {
    public:
        static bool extract(const std::vector<uint8_t>& file_bytes,
                            RollbackReplayInputScript& out) noexcept
        {
            out = {};
            out.file_bytes = file_bytes.size();
            out.file_hash = file_bytes.empty()
                ? 0
                : ReplayTraceFields::fnv1a64(
                    file_bytes.data(), file_bytes.size());

            if (is_consumed_input_sidecar(file_bytes))
                return extract_consumed_input_sidecar(file_bytes, out);

            std::vector<uint8_t> payload;
            if (!unwrap_payload(file_bytes, payload, out))
                return false;

            std::vector<uint8_t> decompressed;
            if (!native_decompress_ulx1(payload, decompressed, out))
                return false;

            if (!extract_native_setup_metadata(decompressed, out))
                return false;

            if (!extract_length_prefixed_streams(decompressed, out))
                return false;

            out.input_frames_p0 = out.player_inputs[0].size();
            out.input_frames_p1 = out.player_inputs[1].size();
            out.input_hash_p0 = hash_u32_stream(out.player_inputs[0]);
            out.input_hash_p1 = hash_u32_stream(out.player_inputs[1]);
            out.ok =
                out.input_block_pairs > 0
                && out.input_frames_p0 > 0
                && out.input_frames_p0 == out.input_frames_p1
                && out.input_hash_p0 != 0
                && out.input_hash_p1 != 0;
            out.failure = out.ok ? "ok" : "replay-input-streams-empty";
            return out.ok;
        }

    private:
        static constexpr uintptr_t kRVA_LuxReplayDecompressUlx1 = 0x2DCE6F0;
        static constexpr uintptr_t kRVA_ReplayListItemInitialize = 0x5799D0;
        static constexpr uintptr_t kRVA_ReplayListItemDestroy = 0x4EEBA0;
        static constexpr uintptr_t kRVA_LuxReplayDeserializeItem = 0x5B17F0;
        static constexpr uintptr_t kRVA_FMemoryFree = 0xD46A00;
        static constexpr uint32_t kReplayWrapperHeaderBytes = 72;
        static constexpr uint32_t kReplayWrapperVersion = 1;
        static constexpr uint32_t kMaxScriptInputMask = 0x3FFF;
        static constexpr uint32_t kMinInputStreamBytes = 256;
        static constexpr uint32_t kMaxInputStreamBytes = 60000;
        static constexpr uint64_t kMaxReplayPayloadBytes =
            64ull * 1024ull * 1024ull;
        static constexpr size_t kNativeReplayListItemBytes = 0x1A00;
        static constexpr size_t kNativeReplayListItemBattleDataOff = 0xA0;
        static constexpr size_t kNativeBattleRuleRandomSeedOff = 0x80;

        struct TArrayByteNative
        {
            uint8_t* data {nullptr};
            int32_t num {0};
            int32_t max {0};
        };
        static_assert(sizeof(TArrayByteNative) == 0x10,
                      "TArray<byte> header layout changed");

        using LuxReplayDecompressUlx1Fn =
            bool(__fastcall*)(TArrayByteNative*, TArrayByteNative*);
        using ReplayListItemInitializeFn = void*(__fastcall*)(void*);
        using ReplayListItemDestroyFn = void(__fastcall*)(void*);
        using LuxReplayDeserializeItemFn =
            bool(__fastcall*)(TArrayByteNative*, void*);
        using FMemoryFreeFn = void(__fastcall*)(void*);

        struct CandidateBlock
        {
            uint32_t offset {0};
            uint32_t byte_count {0};
            uint32_t frame_count {0};
            uint32_t nonzero {0};
            uint32_t distinct {0};
            uint32_t max_value {0};
            uint64_t hash {0};
            std::vector<uint32_t> values {};
        };

        static uint32_t read_le_u32(const uint8_t* p) noexcept
        {
            uint32_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        static uint64_t read_le_u64(const uint8_t* p) noexcept
        {
            uint64_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        static int32_t read_le_i32(const uint8_t* p) noexcept
        {
            int32_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        static uint64_t hash_u32_stream(
            const std::vector<uint32_t>& values) noexcept
        {
            return values.empty() ? 0 : ReplayTraceFields::fnv1a64(
                values.data(), values.size() * sizeof(uint32_t));
        }

        static bool is_consumed_input_sidecar(
            const std::vector<uint8_t>& bytes) noexcept
        {
            return RollbackConsumedInputSidecarHasMagic(bytes);
        }

        static bool extract_consumed_input_sidecar(
            const std::vector<uint8_t>& bytes,
            RollbackReplayInputScript& out) noexcept
        {
            RollbackConsumedInputSidecar sidecar {};
            if (!RollbackConsumedInputSidecarCodec::extract(bytes, sidecar))
            {
                out.failure = sidecar.failure;
                return false;
            }
            out.consumed_input_sidecar = true;
            out.wrapper_header = true;
            out.native_setup_metadata_ok = true;
            out.file_bytes = sidecar.file_bytes;
            out.file_hash = sidecar.file_hash;
            out.payload_bytes = sidecar.payload_bytes;
            out.payload_hash = sidecar.payload_hash;
            out.input_blocks_detected = sidecar.round_pairs.size() * 2u;
            out.input_block_pairs = sidecar.round_pairs.size();
            out.input_frames_p0 = sidecar.input_frames_p0;
            out.input_frames_p1 = sidecar.input_frames_p1;
            out.input_hash_p0 = sidecar.input_hash_p0;
            out.input_hash_p1 = sidecar.input_hash_p1;
            out.setup_stage_index = sidecar.stage_index;
            out.setup_left_chara_id = sidecar.left_chara_id;
            out.setup_right_chara_id = sidecar.right_chara_id;
            out.setup_random_seed = sidecar.random_seed;
            out.source_build_id = sidecar.source_build_id;
            out.source_schema_id = sidecar.source_schema_id;
            out.source_replay_sha256 = sidecar.source_replay_sha256;
            out.source_oracle_sha256 = sidecar.source_oracle_sha256;
            out.bound_artifact_sha256 = sidecar.bound_artifact_sha256;
            out.player_inputs = std::move(sidecar.player_inputs);
            out.round_pairs = std::move(sidecar.round_pairs);
            out.ok = true;
            out.failure = "ok";
            return true;
        }

        static void safe_native_free(FMemoryFreeFn fn, void* ptr) noexcept
        {
            if (!fn || !ptr) return;
            __try
            {
                fn(ptr);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        static bool safe_native_decompress(
            LuxReplayDecompressUlx1Fn fn,
            TArrayByteNative* out,
            TArrayByteNative* input) noexcept
        {
            if (!fn || !out || !input) return false;
            __try
            {
                return fn(out, input);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        static bool safe_native_initialize_replay_item(
            ReplayListItemInitializeFn fn, void* item) noexcept
        {
            if (!fn || !item) return false;
            __try
            {
                fn(item);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        static bool safe_native_deserialize_replay_item(
            LuxReplayDeserializeItemFn fn,
            TArrayByteNative* payload,
            void* item) noexcept
        {
            if (!fn || !payload || !item) return false;
            __try
            {
                return fn(payload, item);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        static bool safe_native_destroy_replay_item(
            ReplayListItemDestroyFn fn, void* item) noexcept
        {
            if (!fn || !item) return false;
            __try
            {
                fn(item);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        static bool unwrap_payload(const std::vector<uint8_t>& file_bytes,
                                   std::vector<uint8_t>& payload,
                                   RollbackReplayInputScript& out) noexcept
        {
            static constexpr char kMagic[8] = {
                'H', 'M', 'R', 'P', 'L', 'Y', '1', '\0'
            };
            if (file_bytes.empty())
            {
                out.failure = "replay-input-file-empty";
                return false;
            }

            const bool has_wrapper =
                file_bytes.size() >= kReplayWrapperHeaderBytes
                && std::memcmp(file_bytes.data(), kMagic, sizeof(kMagic)) == 0;
            out.wrapper_header = has_wrapper;
            if (has_wrapper)
            {
                const uint32_t version = read_le_u32(file_bytes.data() + 8);
                const uint32_t header_bytes =
                    read_le_u32(file_bytes.data() + 12);
                const uint64_t payload_bytes =
                    read_le_u64(file_bytes.data() + 16);
                const uint64_t expected_payload_hash =
                    read_le_u64(file_bytes.data() + 32);
                const int32_t setup_stage =
                    read_le_i32(file_bytes.data() + 56);
                const int32_t setup_left =
                    read_le_i32(file_bytes.data() + 60);
                const int32_t setup_right =
                    read_le_i32(file_bytes.data() + 64);
                if (version != kReplayWrapperVersion ||
                    header_bytes != kReplayWrapperHeaderBytes ||
                    payload_bytes == 0 ||
                    payload_bytes > kMaxReplayPayloadBytes ||
                    file_bytes.size() !=
                        static_cast<size_t>(header_bytes + payload_bytes))
                {
                    out.failure = "invalid-HorseMod-replay-wrapper";
                    return false;
                }
                payload.assign(
                    file_bytes.begin() + kReplayWrapperHeaderBytes,
                    file_bytes.end());
                out.payload_hash =
                    ReplayTraceFields::fnv1a64(
                        payload.data(), payload.size());
                if (out.payload_hash != expected_payload_hash)
                {
                    payload.clear();
                    out.failure = "replay-wrapper-payload-hash-mismatch";
                    return false;
                }
                out.setup_stage_index = setup_stage;
                out.setup_left_chara_id = setup_left;
                out.setup_right_chara_id = setup_right;
            }
            else
            {
                payload = file_bytes;
                out.payload_hash = out.file_hash;
            }

            out.payload_bytes = payload.size();
            out.payload_ulx1 =
                payload.size() >= 8 &&
                std::memcmp(payload.data(), "ULX1", 4) == 0;
            if (!out.payload_ulx1)
            {
                out.failure = "replay-payload-missing-ULX1";
                return false;
            }
            return true;
        }

        static bool native_decompress_ulx1(
            const std::vector<uint8_t>& payload,
            std::vector<uint8_t>& decompressed,
            RollbackReplayInputScript& out) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base)
            {
                out.failure = "native-image-base-unavailable";
                return false;
            }
            auto* decompress =
                reinterpret_cast<LuxReplayDecompressUlx1Fn>(
                    base + kRVA_LuxReplayDecompressUlx1);
            auto* free_fn =
                reinterpret_cast<FMemoryFreeFn>(base + kRVA_FMemoryFree);

            TArrayByteNative input {};
            input.data = const_cast<uint8_t*>(payload.data());
            input.num = static_cast<int32_t>(payload.size());
            input.max = input.num;
            TArrayByteNative output {};
            const bool ok = safe_native_decompress(
                decompress, &output, &input);
            out.native_decompress_ok = ok;
            if (!ok)
            {
                safe_native_free(free_fn, output.data);
                out.failure = "native-ULX1-decompress-failed";
                return false;
            }
            if (!output.data || output.num <= 0 ||
                static_cast<uint64_t>(output.num) > kMaxReplayPayloadBytes)
            {
                safe_native_free(free_fn, output.data);
                out.failure = "native-ULX1-decompress-output-invalid";
                return false;
            }

            bool copied = false;
            try
            {
                decompressed.assign(
                    static_cast<size_t>(output.num), 0);
                copied = SafeReadBytes(
                    output.data, decompressed.data(), decompressed.size());
            }
            catch (const std::bad_alloc&)
            {
                copied = false;
            }
            safe_native_free(free_fn, output.data);
            if (!copied)
            {
                decompressed.clear();
                out.failure = "native-ULX1-decompress-copy-failed";
                return false;
            }
            out.decompressed_bytes = decompressed.size();
            out.decompressed_hash = ReplayTraceFields::fnv1a64(
                decompressed.data(), decompressed.size());
            return true;
        }

        static bool extract_native_setup_metadata(
            const std::vector<uint8_t>& decompressed,
            RollbackReplayInputScript& out) noexcept
        {
            const uintptr_t base = NativeBinding::imageBase();
            if (!base || decompressed.empty()
                || decompressed.size() > static_cast<size_t>(INT32_MAX))
            {
                out.failure = "native-replay-setup-metadata-unavailable";
                return false;
            }

            auto* initialize = reinterpret_cast<ReplayListItemInitializeFn>(
                base + kRVA_ReplayListItemInitialize);
            auto* deserialize = reinterpret_cast<LuxReplayDeserializeItemFn>(
                base + kRVA_LuxReplayDeserializeItem);
            auto* destroy = reinterpret_cast<ReplayListItemDestroyFn>(
                base + kRVA_ReplayListItemDestroy);
            alignas(16) std::array<uint8_t, kNativeReplayListItemBytes>
                item {};
            bool initialized = false;
            bool deserialized = false;
            bool destroyed = false;
            uint32_t seed = 0;

            initialized = safe_native_initialize_replay_item(
                initialize, item.data());
            if (initialized)
            {
                TArrayByteNative payload {};
                payload.data = const_cast<uint8_t*>(decompressed.data());
                payload.num = static_cast<int32_t>(decompressed.size());
                payload.max = payload.num;
                deserialized = safe_native_deserialize_replay_item(
                    deserialize, &payload, item.data());
                if (deserialized)
                {
                    std::memcpy(
                        &seed,
                        item.data() + kNativeReplayListItemBattleDataOff
                            + kNativeBattleRuleRandomSeedOff,
                        sizeof(seed));
                }
                destroyed = safe_native_destroy_replay_item(
                    destroy, item.data());
            }

            out.native_setup_metadata_ok = initialized && deserialized
                && destroyed && seed != 0;
            out.setup_random_seed = out.native_setup_metadata_ok ? seed : 0;
            if (!out.native_setup_metadata_ok)
            {
                out.failure = !initialized
                    ? "native-replay-item-initialize-failed"
                    : (!deserialized
                        ? "native-replay-item-deserialize-failed"
                        : (!destroyed
                            ? "native-replay-item-destroy-failed"
                            : "native-replay-random-seed-invalid"));
            }
            return out.native_setup_metadata_ok;
        }

        static bool input_block_candidate(
            const std::vector<uint8_t>& raw,
            uint32_t offset,
            CandidateBlock& out) noexcept
        {
            if (offset + sizeof(uint32_t) > raw.size())
                return false;
            const uint32_t byte_count = read_le_u32(raw.data() + offset);
            if (byte_count < kMinInputStreamBytes ||
                byte_count > kMaxInputStreamBytes ||
                (byte_count % sizeof(uint32_t)) != 0 ||
                offset + sizeof(uint32_t) + byte_count > raw.size())
            {
                return false;
            }

            const uint32_t frame_count =
                byte_count / static_cast<uint32_t>(sizeof(uint32_t));
            std::array<bool, kMaxScriptInputMask + 1> seen {};
            uint32_t nonzero = 0;
            uint32_t distinct = 0;
            uint32_t max_value = 0;
            std::vector<uint32_t> values;
            try
            {
                values.reserve(frame_count);
                for (uint32_t i = 0; i < frame_count; ++i)
                {
                    const uint32_t v = read_le_u32(
                        raw.data() + offset + sizeof(uint32_t)
                        + static_cast<size_t>(i) * sizeof(uint32_t));
                    if (v > kMaxScriptInputMask)
                        return false;
                    values.push_back(v);
                    if (v != 0) ++nonzero;
                    if (!seen[v])
                    {
                        seen[v] = true;
                        ++distinct;
                    }
                    if (v > max_value) max_value = v;
                }
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }

            if (nonzero < 20 || distinct < 5)
                return false;

            out.offset = offset;
            out.byte_count = byte_count;
            out.frame_count = frame_count;
            out.nonzero = nonzero;
            out.distinct = distinct;
            out.max_value = max_value;
            out.hash = hash_u32_stream(values);
            out.values = std::move(values);
            return true;
        }

        static bool extract_length_prefixed_streams(
            const std::vector<uint8_t>& raw,
            RollbackReplayInputScript& out) noexcept
        {
            std::vector<CandidateBlock> candidates;
            try
            {
                for (uint32_t offset = 0;
                     offset + sizeof(uint32_t) < raw.size();
                     offset += sizeof(uint32_t))
                {
                    CandidateBlock block {};
                    if (input_block_candidate(raw, offset, block))
                        candidates.push_back(std::move(block));
                }
            }
            catch (const std::bad_alloc&)
            {
                out.failure = "replay-input-candidate-allocation-failed";
                return false;
            }

            std::sort(
                candidates.begin(), candidates.end(),
                [](const CandidateBlock& a, const CandidateBlock& b)
                {
                    return a.offset < b.offset;
                });

            std::vector<CandidateBlock> blocks;
            try
            {
                uint32_t skip_until = 0;
                for (auto& block : candidates)
                {
                    if (block.offset < skip_until)
                        continue;
                    skip_until =
                        block.offset + sizeof(uint32_t) + block.byte_count;
                    blocks.push_back(std::move(block));
                }
            }
            catch (const std::bad_alloc&)
            {
                out.failure = "replay-input-block-allocation-failed";
                return false;
            }
            out.input_blocks_detected = blocks.size();

            std::vector<RollbackReplayInputRoundPair> pairs;
            std::array<std::vector<uint32_t>, 2> streams {};
            std::vector<bool> used(blocks.size(), false);
            try
            {
                for (size_t i = 0; i < blocks.size(); ++i)
                {
                    if (used[i]) continue;
                    size_t match = blocks.size();
                    for (size_t j = i + 1; j < blocks.size(); ++j)
                    {
                        if (!used[j] &&
                            blocks[j].byte_count == blocks[i].byte_count)
                        {
                            match = j;
                            break;
                        }
                    }
                    if (match == blocks.size())
                        continue;

                    used[i] = true;
                    used[match] = true;
                    RollbackReplayInputRoundPair pair {};
                    pair.player0_offset = blocks[i].offset;
                    pair.player1_offset = blocks[match].offset;
                    pair.byte_count = blocks[i].byte_count;
                    pair.frame_count = blocks[i].frame_count;
                    pair.player0_hash = blocks[i].hash;
                    pair.player1_hash = blocks[match].hash;
                    pairs.push_back(pair);
                    streams[0].insert(
                        streams[0].end(),
                        blocks[i].values.begin(), blocks[i].values.end());
                    streams[1].insert(
                        streams[1].end(),
                        blocks[match].values.begin(),
                        blocks[match].values.end());
                }
            }
            catch (const std::bad_alloc&)
            {
                out.failure = "replay-input-stream-allocation-failed";
                return false;
            }

            if (pairs.empty() || streams[0].empty() ||
                streams[0].size() != streams[1].size())
            {
                out.failure = "paired-replay-input-streams-not-found";
                return false;
            }

            out.input_block_pairs = pairs.size();
            out.round_pairs = std::move(pairs);
            out.player_inputs = std::move(streams);
            return true;
        }
    };
}
