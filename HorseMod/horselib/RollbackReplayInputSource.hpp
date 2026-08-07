// ============================================================================
// Horse::RollbackReplayInputSource
//
// Preloads one replay round before production ownership and provides bounded,
// allocation-free compact inputs on the game thread.
// ============================================================================

#pragma once

#include "RollbackLaunchContract.hpp"
#include "RollbackReplayInputScript.hpp"

#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Horse
{
    class RollbackReplayInputSource
    {
    public:
        bool prepare(
            const std::string& path,
            RollbackReplayInputConfig& config,
            uint64_t expected_build_id,
            uint64_t expected_schema_id) noexcept
        {
            clear();
            if (!config.enabled) return true;
            if (path.empty() || !config.requested_valid())
                return fail("replay-input-config-invalid");

            std::vector<uint8_t> bytes;
            if (!read_file(path, bytes))
                return fail("replay-input-file-read-failed");
            std::array<uint8_t, 32> digest {};
            if (!sha256(bytes, digest))
                return fail("replay-input-sha256-failed");
            if (digest != config.file_sha256)
                return fail("replay-input-sha256-mismatch");
            if (!RollbackReplayInputScriptExtractor::extract(bytes, m_script))
                return fail(m_script.failure);
            if (m_script.consumed_input_sidecar
                && !RollbackConsumedInputIdentityMatches(
                    m_script, expected_build_id, expected_schema_id))
            {
                return fail("consumed-input-sidecar-build-schema-mismatch");
            }
            if (!m_script.native_setup_metadata_ok
                || m_script.setup_random_seed != config.replay_random_seed)
            {
                return fail("replay-input-random-seed-mismatch");
            }
            if (!select_round(config.round_index, config, true))
                return false;
            m_ready = true;
            m_failure = "ok";
            return true;
        }

        // The authenticated file digest covers every extracted round. Move to
        // the next authored pair without file I/O or allocation between stock
        // online rounds.
        bool advance_round(RollbackReplayInputConfig& config) noexcept
        {
            uint32_t next_round = 0;
            if (!m_ready || !RollbackNextReplayRoundIndex(
                    config.round_index,
                    static_cast<uint32_t>(m_script.round_pairs.size()),
                    next_round))
                return fail("replay-input-next-round-unavailable");
            return select_round(next_round, config, false);
        }

        bool has_round(uint32_t round_index) const noexcept
        {
            return m_ready && round_index < m_script.round_pairs.size();
        }

        bool current_rng_baseline(
            RollbackReplayRngBaseline& baseline) const noexcept
        {
            baseline = {};
            if (!m_ready || !m_script.consumed_input_sidecar
                || m_selected_round >= m_script.round_pairs.size())
                return false;
            baseline = m_script.round_pairs[m_selected_round].rng_baseline;
            return RollbackReplayRngBaselineValid(baseline);
        }

        bool current_motion_pose_baseline(
            RollbackMotionPoseResidueSnapshot& baseline,
            uint64_t& baseline_hash) const noexcept
        {
            baseline = {};
            baseline_hash = 0;
            if (!m_ready || !m_script.consumed_input_sidecar
                || m_selected_round >= m_script.round_pairs.size())
                return false;
            const auto& selected = m_script.round_pairs[m_selected_round];
            baseline = selected.motion_pose_residue;
            baseline_hash = selected.motion_pose_residue_hash;
            return RollbackReplayMotionPoseBaselineValid(
                baseline, baseline_hash);
        }

        bool consumed_input_sidecar() const noexcept
        {
            return m_ready && m_script.consumed_input_sidecar;
        }

        bool source_replay_sha256(
            std::array<uint8_t, 32>& digest) const noexcept
        {
            digest = {};
            if (!m_ready || !m_script.consumed_input_sidecar)
                return false;
            digest = m_script.source_replay_sha256;
            return true;
        }

        bool input(
            const RollbackReplayInputConfig& config,
            uint8_t native_slot,
            uint32_t submission_frame,
            uint16_t input_delay,
            uint32_t& value,
            uint32_t& round_index) const noexcept
        {
            value = 0;
            round_index = 0;
            if (!m_ready || native_slot >= 2
                || !config.input_index(
                    submission_frame, input_delay, round_index)
                || round_index >= m_round_frames)
            {
                return false;
            }
            const uint8_t replay_player =
                config.replay_player_for_native_slot(native_slot);
            const uint64_t absolute = static_cast<uint64_t>(m_round_prefix)
                + round_index;
            if (replay_player >= 2
                || absolute >= m_script.player_inputs[replay_player].size())
                return false;
            value = m_script.player_inputs[replay_player][
                static_cast<size_t>(absolute)];
            return true;
        }

        bool consumed_input(
            const RollbackReplayInputConfig& config,
            uint8_t native_slot,
            uint32_t logical_frame,
            uint16_t input_delay,
            uint32_t& value,
            uint32_t& round_index) const noexcept
        {
            value = 0;
            round_index = 0;
            if (!m_ready || native_slot >= 2
                || !config.consumed_index(
                    logical_frame, input_delay, round_index)
                || round_index >= m_round_frames)
            {
                return false;
            }
            const uint8_t replay_player =
                config.replay_player_for_native_slot(native_slot);
            const uint64_t absolute = static_cast<uint64_t>(m_round_prefix)
                + round_index;
            if (replay_player >= 2
                || absolute >= m_script.player_inputs[replay_player].size())
                return false;
            value = m_script.player_inputs[replay_player][
                static_cast<size_t>(absolute)];
            return true;
        }

        bool consumed_input_or_neutral_tail(
            const RollbackReplayInputConfig& config,
            uint8_t native_slot,
            uint32_t logical_frame,
            uint16_t input_delay,
            uint32_t& value,
            uint32_t& round_index,
            bool& neutral_tail) const noexcept
        {
            value = 0;
            round_index = 0;
            neutral_tail = false;
            bool authored = false;
            if (!m_ready || native_slot >= 2
                || !config.consumed_index_or_neutral_tail(
                    logical_frame, input_delay, round_index, authored))
            {
                return false;
            }
            if (!authored)
            {
                neutral_tail = true;
                return true;
            }
            const uint8_t replay_player =
                config.replay_player_for_native_slot(native_slot);
            const uint64_t absolute = static_cast<uint64_t>(m_round_prefix)
                + round_index;
            if (replay_player >= 2
                || absolute >= m_script.player_inputs[replay_player].size())
                return false;
            value = m_script.player_inputs[replay_player][
                static_cast<size_t>(absolute)];
            return true;
        }

        void clear() noexcept
        {
            m_script = {};
            m_round_prefix = 0;
            m_round_frames = 0;
            m_selected_round = 0;
            m_ready = false;
            m_failure = "not-loaded";
        }

        bool ready() const noexcept { return m_ready; }
        const char* failure() const noexcept { return m_failure; }

    private:
        bool select_round(
            uint32_t round_index,
            RollbackReplayInputConfig& config,
            bool validate_requested) noexcept
        {
            if (round_index >= m_script.round_pairs.size())
                return fail("replay-input-round-out-of-range");

            uint64_t prefix = 0;
            for (uint32_t round = 0; round < round_index; ++round)
                prefix += m_script.round_pairs[round].frame_count;
            const auto& selected = m_script.round_pairs[round_index];
            if (prefix > UINT32_MAX
                || prefix + selected.frame_count
                    > m_script.player_inputs[0].size()
                || prefix + selected.frame_count
                    > m_script.player_inputs[1].size())
            {
                return fail("replay-input-round-layout-invalid");
            }
            if (validate_requested
                && config.round_frame_count != 0
                && config.round_frame_count != selected.frame_count)
                return fail("replay-input-round-frame-count-mismatch");
            for (size_t player = 0; player < 2; ++player)
            {
                const uint64_t selected_hash = player == 0
                    ? selected.player0_hash : selected.player1_hash;
                if (validate_requested
                    && config.round_input_hash[player] != 0
                    && config.round_input_hash[player] != selected_hash)
                    return fail("replay-input-round-hash-mismatch");
                config.round_input_hash[player] = selected_hash;
            }
            config.round_index = round_index;
            uint32_t round_start_frame = 0;
            if (!config.start_frame_for_round(
                    round_index, round_start_frame))
                return fail("replay-input-round-origin-unavailable");
            config.start_frame = round_start_frame;
            config.round_frame_count = selected.frame_count;
            if (!config.resolved_valid())
                return fail("replay-input-resolved-config-invalid");

            m_round_prefix = static_cast<uint32_t>(prefix);
            m_round_frames = selected.frame_count;
            m_selected_round = round_index;
            m_failure = "ok";
            return true;
        }

        bool fail(const char* reason) noexcept
        {
            m_ready = false;
            m_failure = reason ? reason : "replay-input-load-failed";
            return false;
        }

        static bool read_file(
            const std::string& path,
            std::vector<uint8_t>& bytes) noexcept
        {
            try
            {
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                if (!file) return false;
                const std::streamoff size = file.tellg();
                if (size <= 0 || size > 64ll * 1024ll * 1024ll)
                    return false;
                bytes.resize(static_cast<size_t>(size));
                file.seekg(0, std::ios::beg);
                return static_cast<bool>(file.read(
                    reinterpret_cast<char*>(bytes.data()), size));
            }
            catch (...)
            {
                bytes.clear();
                return false;
            }
        }

        static bool sha256(
            const std::vector<uint8_t>& bytes,
            std::array<uint8_t, 32>& digest) noexcept
        {
            digest.fill(0);
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            DWORD object_bytes = 0;
            DWORD hash_bytes = 0;
            DWORD result_bytes = 0;
            std::vector<uint8_t> object;
            bool ok = false;
            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                    &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
                return false;
            try
            {
                if (BCRYPT_SUCCESS(BCryptGetProperty(
                        algorithm, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&object_bytes),
                        sizeof(object_bytes), &result_bytes, 0))
                    && BCRYPT_SUCCESS(BCryptGetProperty(
                        algorithm, BCRYPT_HASH_LENGTH,
                        reinterpret_cast<PUCHAR>(&hash_bytes),
                        sizeof(hash_bytes), &result_bytes, 0))
                    && hash_bytes == digest.size())
                {
                    object.resize(object_bytes);
                    ok = BCRYPT_SUCCESS(BCryptCreateHash(
                            algorithm, &hash, object.data(), object_bytes,
                            nullptr, 0, 0))
                        && BCRYPT_SUCCESS(BCryptHashData(
                            hash, const_cast<PUCHAR>(bytes.data()),
                            static_cast<ULONG>(bytes.size()), 0))
                        && BCRYPT_SUCCESS(BCryptFinishHash(
                            hash, digest.data(),
                            static_cast<ULONG>(digest.size()), 0));
                }
            }
            catch (...)
            {
                ok = false;
            }
            if (hash) BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            if (!ok) digest.fill(0);
            return ok;
        }

        RollbackReplayInputScript m_script {};
        uint32_t m_round_prefix {0};
        uint32_t m_round_frames {0};
        uint32_t m_selected_round {0};
        bool m_ready {false};
        const char* m_failure {"not-loaded"};
    };
}
