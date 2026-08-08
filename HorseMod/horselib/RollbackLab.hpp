// ============================================================================
// Horse::RollbackLab
//
// User-facing control shell for the rollback implementation plan. The lab is
// disabled by default. When explicitly enabled, probes may snapshot, restore,
// and resimulate SC6 state on the game thread, then restore their start state.
// ============================================================================

#pragma once

#include "RollbackBetaConfig.hpp"
#include "RollbackController.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>
#include <shellapi.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Horse
{
    class RollbackLab
    {
    public:
        static RollbackLab& instance() noexcept
        {
            static RollbackLab s_instance;
            return s_instance;
        }

        void configure_from_command_line_once() noexcept
        {
            if (m_cmd_consumed.exchange(true, std::memory_order_acq_rel))
                return;

            try
            {
            RollbackLabConfig cfg{};
            cfg.source = "command-line";
            std::wstring value;
            cfg.enabled =
                has_flag(L"--horsemod-rollback-lab")
                || has_flag(L"--horsemod-rollback-enable");
            cfg.trace_enabled = has_flag(L"--horsemod-rollback-trace");
            cfg.native_lifecycle_trace_frame_input_log_only = has_flag(
                L"--horsemod-rollback-frame-input-log-trace-only");
            if (option_value(L"--horsemod-rollback-case", value))
                cfg.test_case = rollback_case_from_string(wide_to_utf8(value));
            if (option_value(L"--horsemod-rollback-window", value))
                cfg.rollback_window = parse_u32(value, cfg.rollback_window);
            if (option_value(L"--horsemod-rollback-seed", value))
                cfg.seed = parse_u32(value, cfg.seed);
            cfg.production.enabled =
                has_flag(L"--horsemod-rollback-production");
            if (cfg.production.enabled)
            {
                cfg.enabled = true;
                cfg.test_case = RollbackLabCase::Production;
            }
            if (option_value(L"--horsemod-rollback-bind-address", value))
                cfg.production.bind_address = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-bind-port", value))
                cfg.production.bind_port = static_cast<uint16_t>(
                    parse_u32(value, cfg.production.bind_port));
            if (option_value(L"--horsemod-rollback-peer-address", value))
                cfg.production.peer_address = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-peer-port", value))
                cfg.production.peer_port = static_cast<uint16_t>(
                    parse_u32(value, cfg.production.peer_port));
            if (option_value(L"--horsemod-rollback-local-slot", value))
                cfg.production.local_player_slot = static_cast<uint8_t>(
                    parse_u32(value, cfg.production.local_player_slot));
            if (option_value(
                    L"--horsemod-rollback-native-input-slot", value))
            {
                cfg.production.native_input_source_slot =
                    static_cast<uint8_t>(parse_u32(
                        value,
                        cfg.production.native_input_source_slot));
            }
            if (option_value(
                    L"--horsemod-rollback-launch-left-character", value))
                cfg.launch_left_character_override = parse_i32(
                    value, cfg.launch_left_character_override);
            if (option_value(
                    L"--horsemod-rollback-launch-right-character", value))
                cfg.launch_right_character_override = parse_i32(
                    value, cfg.launch_right_character_override);
            if (option_value(
                    L"--horsemod-rollback-launch-left-character-code", value))
                cfg.launch_left_character_code_override = wide_to_utf8(value);
            if (option_value(
                    L"--horsemod-rollback-launch-right-character-code", value))
                cfg.launch_right_character_code_override = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-launch-stage", value))
                cfg.launch_stage_override = parse_i32(
                    value, cfg.launch_stage_override);
            if (option_value(
                    L"--horsemod-rollback-launch-rounds-to-win", value))
                cfg.launch_rounds_to_win_override = parse_i32(
                    value, cfg.launch_rounds_to_win_override);
            if (option_value(
                    L"--horsemod-rollback-lifecycle-mode", value))
            {
                cfg.production.lifecycle_mode =
                    rollback_lifecycle_mode_from_string(
                        wide_to_utf8(value));
            }
            if (option_value(L"--horsemod-rollback-production-local-peer", value))
                cfg.production.local_peer = static_cast<uint8_t>(
                    parse_u32(value, cfg.production.local_peer));
            if (option_value(L"--horsemod-rollback-production-remote-peer", value))
                cfg.production.remote_peer = static_cast<uint8_t>(
                    parse_u32(value, cfg.production.remote_peer));
            if (option_value(L"--horsemod-rollback-secret", value))
                cfg.production.secret = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-input-delay", value))
                cfg.production.input_delay = static_cast<uint16_t>(
                    parse_u32(value, cfg.production.input_delay));
            cfg.production.deterministic_input.enabled =
                has_flag(L"--horsemod-rollback-deterministic-input");
            if (option_value(
                    L"--horsemod-rollback-fixture-id", value))
                cfg.production.deterministic_input.fixture_id = parse_u32(
                    value, cfg.production.deterministic_input.fixture_id);
            if (option_value(
                    L"--horsemod-rollback-correction-start", value))
                cfg.production.deterministic_input.correction_start =
                    parse_u32(value,
                        cfg.production.deterministic_input.correction_start);
            if (option_value(
                    L"--horsemod-rollback-hold-updates", value))
                cfg.production.deterministic_input.hold_updates =
                    static_cast<uint16_t>(parse_u32(value,
                        cfg.production.deterministic_input.hold_updates));
            if (option_value(
                    L"--horsemod-rollback-prediction-lead", value))
                cfg.production.deterministic_input.prediction_lead_updates =
                    static_cast<uint16_t>(parse_u32(value,
                        cfg.production.deterministic_input
                            .prediction_lead_updates));
            cfg.production.replay_input.enabled =
                has_flag(L"--horsemod-rollback-replay-input");
            if (option_value(
                    L"--horsemod-rollback-replay-input-round", value))
                cfg.production.replay_input.round_index = parse_u32(
                    value, cfg.production.replay_input.round_index);
            if (option_value(
                    L"--horsemod-rollback-replay-input-start", value))
                cfg.production.replay_input.start_frame = parse_u32(
                    value, cfg.production.replay_input.start_frame);
            cfg.production.replay_input.swap_players =
                has_flag(L"--horsemod-rollback-replay-input-swap-players");
            if (option_value(
                    L"--horsemod-rollback-replay-input-sha256", value))
            {
                if (!parse_sha256_ascii(
                        wide_to_utf8(value),
                        cfg.production.replay_input.file_sha256))
                    cfg.production.replay_input.file_sha256 = {};
            }
            if (option_value(
                    L"--horsemod-rollback-network-profile", value))
            {
                RollbackNetworkProfileKind profile {};
                cfg.production.network_profile =
                    TryParseRollbackNetworkProfile(
                        lower_ascii(wide_to_utf8(value)), profile)
                    ? profile
                    : static_cast<RollbackNetworkProfileKind>(0xFF);
            }
            if (option_value(
                    L"--horsemod-rollback-fault-seed", value))
                cfg.production.fault_seed = static_cast<uint32_t>(
                    parse_u64(value, cfg.production.fault_seed));
            if (option_value(L"--horsemod-rollback-build-id", value))
                cfg.production.expected_build_id = parse_u64(
                    value, cfg.production.expected_build_id);
            if (option_value(L"--horsemod-rollback-schema-id", value))
                cfg.production.expected_schema_id = parse_u64(
                    value, cfg.production.expected_schema_id);
            cfg.live_activation_operator_enable =
                has_flag(L"--horsemod-rollback-live-activation-arm");
            if (option_value(L"--horsemod-rollback-live-source-peer", value))
                cfg.live_activation_source_peer = static_cast<uint8_t>(
                    parse_u32(value, cfg.live_activation_source_peer));
            if (option_value(L"--horsemod-rollback-live-destination-peer",
                             value))
                cfg.live_activation_destination_peer = static_cast<uint8_t>(
                    parse_u32(value, cfg.live_activation_destination_peer));
            if (option_value(L"--horsemod-rollback-live-session-id", value))
                cfg.live_activation_session_id =
                    parse_u64(value, cfg.live_activation_session_id);
            if (option_value(L"--horsemod-rollback-client-role", value))
                cfg.client_role = wide_to_utf8(value);
            if (option_value(
                    L"--horsemod-rollback-qualification-contract", value))
                cfg.qualification_contract_version = parse_u32(
                    value, cfg.qualification_contract_version);
            if (option_value(
                    L"--horsemod-rollback-qualification-run", value))
                cfg.qualification_run_id = wide_to_utf8(value);
            if (option_value(
                    L"--horsemod-rollback-qualification-case", value))
                cfg.qualification_case_id = wide_to_utf8(value);
            if (option_value(
                    L"--horsemod-rollback-qualification-segment", value))
                cfg.qualification_segment_id = wide_to_utf8(value);
            if (option_value(
                    L"--horsemod-rollback-qualification-schedule", value))
                cfg.qualification_schedule_hash = wide_to_utf8(value);
            if (option_value(
                    L"--horsemod-rollback-qualification-content", value))
                cfg.qualification_content_sha256 = wide_to_utf8(value);
            if (option_value(
                    L"--horsemod-rollback-qualification-protocol", value))
                cfg.qualification_protocol_version = parse_u32(
                    value, cfg.qualification_protocol_version);
            if (option_value(
                    L"--horsemod-rollback-qualification-snapshot", value))
                cfg.qualification_snapshot_version = parse_u32(
                    value, cfg.qualification_snapshot_version);
            if (option_value(L"--horsemod-rollback-sandbox-root", value))
                cfg.sandbox_root = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-sandbox-box", value))
                cfg.sandbox_box = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-local-peer", value))
                cfg.local_peer_id =
                    static_cast<uint8_t>(parse_u32(value, cfg.local_peer_id));
            if (option_value(L"--horsemod-rollback-remote-peer", value))
                cfg.remote_peer_id =
                    static_cast<uint8_t>(parse_u32(value, cfg.remote_peer_id));
            if (option_value(L"--horsemod-rollback-sidecar-local-port", value))
                cfg.sidecar_local_port =
                    static_cast<uint16_t>(
                        parse_u32(value, cfg.sidecar_local_port));
            if (option_value(L"--horsemod-rollback-sidecar-remote-port", value))
                cfg.sidecar_remote_port =
                    static_cast<uint16_t>(
                        parse_u32(value, cfg.sidecar_remote_port));
            if (option_value(L"--horsemod-rollback-sidecar-remote-addr", value))
                cfg.sidecar_remote_addr = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-activation-token", value))
                cfg.activation_token = wide_to_utf8(value);
            cfg.observe_gameflow_requested =
                has_flag(L"--horsemod-rollback-observe-gameflow");
            cfg.observe_gameflow_process_events =
                has_flag(L"--horsemod-rollback-observe-gameflow-process-events");
            if (has_flag(L"--horsemod-rollback-observe-gameflow-no-process-events"))
                cfg.observe_gameflow_process_events = false;
            cfg.online_stage_requested =
                has_flag(L"--horsemod-rollback-online-stage");
            cfg.online_stage_cleanup_only =
                has_flag(L"--horsemod-rollback-online-stage-cleanup-only");
            cfg.online_stage_wait_host_room_ready_marker =
                has_flag(L"--horsemod-rollback-online-stage-wait-host-room-ready-marker");
            if (option_value(
                    L"--horsemod-rollback-main-user-id-override", value))
                cfg.online_stage_main_user_id_override =
                    parse_i32(
                        value,
                        cfg.online_stage_main_user_id_override);
            if (option_value(L"--horsemod-rollback-host-room-ready-marker",
                             value))
                cfg.online_stage_host_room_ready_marker =
                    wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-online-stage-goal", value))
                cfg.online_stage_goal = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-replay-input-file", value))
                cfg.replay_input_file = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-main-menu-player-match-route",
                             value))
                cfg.main_menu_player_match_route = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-local-replay-player", value))
                cfg.local_replay_player =
                    static_cast<uint8_t>(parse_u32(value, cfg.local_replay_player));
            if (option_value(L"--horsemod-rollback-remote-replay-player", value))
                cfg.remote_replay_player =
                    static_cast<uint8_t>(parse_u32(value, cfg.remote_replay_player));
            if (option_value(L"--horsemod-rollback-divergence-frame", value))
                cfg.replay_divergence_frame =
                    parse_u32(value, cfg.replay_divergence_frame);
            if (option_value(L"--horsemod-rollback-divergence-window", value))
                cfg.replay_divergence_window =
                    parse_u32(value, cfg.replay_divergence_window);
            if (option_value(L"--horsemod-rollback-output", value))
                cfg.output_path = wide_to_utf8(value);
            if (option_value(L"--horsemod-rollback-request-id", value))
                cfg.request_id = wide_to_utf8(value);

            if (cfg.production.enabled)
            {
                cfg.enabled = true;
                cfg.test_case = RollbackLabCase::Production;
            }

            if (!cfg.enabled && !cfg.trace_enabled)
            {
                const std::wstring request_path = request_file_path();
                if (!request_path.empty()
                    && GetFileAttributesW(request_path.c_str())
                        != INVALID_FILE_ATTRIBUTES)
                {
                    cfg.source = "default-disabled";
                    m_controller.configure(std::move(cfg));
                    consume_request_file();
                    return;
                }
                if (configure_beta_file())
                    return;
                cfg.source = "default-disabled";
                m_controller.configure(std::move(cfg));
                consume_request_file();
                return;
            }

            if (cfg.trace_enabled)
                ReplayDebugTrace::instance().set_enabled(true);
            m_controller.configure(std::move(cfg));
            }
            catch (...)
            {
                m_controller.reject_configuration_failure(
                    "command-line-config-allocation-failed");
            }
        }

        void shutdown() noexcept
        {
            m_controller.shutdown();
        }

        RollbackModuleUnloadResult prepare_for_module_unload() noexcept
        {
            return m_controller.prepare_for_module_unload();
        }

        void service_game_thread() noexcept
        {
            consume_request_file();
            const int pending_enable =
                m_pending_ui_enable.exchange(-1, std::memory_order_acq_rel);
            if (pending_enable >= 0)
            {
                // Present/ImGui only publishes this request. Controller,
                // socket, Gekko and native-hook lifecycle work stays here.
                m_controller.set_enabled_from_ui(pending_enable != 0);
            }
            m_controller.service_game_thread();
            publish_ui_status_from_game_thread();
        }

        bool enabled() const noexcept
        {
            return m_ui_enabled.load(std::memory_order_acquire);
        }

        void render_imgui_developer_panel()
        {
            const int pending_enable =
                m_pending_ui_enable.load(std::memory_order_acquire);
            bool enabled_ui = pending_enable >= 0
                ? pending_enable != 0
                : m_ui_enabled.load(std::memory_order_acquire);
            if (ImGui::Checkbox("Rollback lab##dev_rollback_lab", &enabled_ui))
                m_pending_ui_enable.store(
                    enabled_ui ? 1 : 0, std::memory_order_release);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(
                "Developer-only rollback lab. When enabled, probes may\n"
                "snapshot, restore, and resimulate SC6 state on the game thread.");

            ImGui::SameLine(0.0f, 20.0f);
            const auto test_case = static_cast<RollbackLabCase>(
                m_ui_test_case.load(std::memory_order_acquire));
            ImGui::TextDisabled("%s", rollback_case_name(test_case));

            ImGui::TextDisabled("Window: %u  Seed: 0x%08X  Ticks: %llu",
                                m_ui_rollback_window.load(
                                    std::memory_order_acquire),
                                m_ui_seed.load(std::memory_order_acquire),
                                static_cast<unsigned long long>(
                                    m_ui_service_ticks.load(
                                        std::memory_order_acquire)));
            ImGui::TextDisabled("Manifest: %zu entries  hash 0x%llX",
                                m_ui_manifest_entries.load(
                                    std::memory_order_acquire),
                                static_cast<unsigned long long>(
                                    m_ui_manifest_hash.load(
                                        std::memory_order_acquire)));
        }

    private:
        RollbackLab() = default;
        RollbackLab(const RollbackLab&) = delete;
        RollbackLab& operator=(const RollbackLab&) = delete;

        void publish_ui_status_from_game_thread() noexcept
        {
            const RollbackLabConfig& cfg = m_controller.config();
            const RollbackSnapshotManifest& manifest = m_controller.manifest();
            m_ui_enabled.store(m_controller.enabled(), std::memory_order_release);
            m_ui_test_case.store(
                static_cast<uint32_t>(cfg.test_case),
                std::memory_order_release);
            m_ui_rollback_window.store(
                cfg.rollback_window, std::memory_order_release);
            m_ui_seed.store(cfg.seed, std::memory_order_release);
            m_ui_service_ticks.store(
                m_controller.service_ticks(), std::memory_order_release);
            m_ui_manifest_entries.store(
                manifest.entries.size(), std::memory_order_release);
            m_ui_manifest_hash.store(
                manifest.coverage_hash(), std::memory_order_release);
        }

        static std::vector<std::wstring> argv()
        {
            int argc = 0;
            LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &argc);
            struct LocalArgv final
            {
                LPWSTR* value {nullptr};
                ~LocalArgv() noexcept
                {
                    if (value) LocalFree(value);
                }
            } raw_guard {raw};
            std::vector<std::wstring> out;
            if (!raw) return out;
            out.reserve(static_cast<size_t>(argc));
            for (int i = 0; i < argc; ++i)
                out.emplace_back(raw[i] ? raw[i] : L"");
            return out;
        }

        static std::wstring parent_dir(std::wstring path)
        {
            while (!path.empty()
                   && (path.back() == L'\\' || path.back() == L'/'))
                path.pop_back();
            const size_t slash = path.find_last_of(L"\\/");
            if (slash == std::wstring::npos) return {};
            path.resize(slash + 1);
            return path;
        }

        static std::wstring leaf_dir(std::wstring path)
        {
            while (!path.empty()
                   && (path.back() == L'\\' || path.back() == L'/'))
                path.pop_back();
            const size_t slash = path.find_last_of(L"\\/");
            if (slash == std::wstring::npos) return path;
            return path.substr(slash + 1);
        }

        static std::wstring module_path()
        {
            HMODULE h = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&RollbackLab::module_path),
                    &h)
                || !h)
                return {};
            wchar_t buf[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(h, buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return {};
            return buf;
        }

        static std::wstring request_file_path()
        {
            std::wstring root = parent_dir(module_path());
            if (root.empty()) return {};
            if (_wcsicmp(leaf_dir(root).c_str(), L"dlls") == 0)
                root = parent_dir(root);
            if (root.empty()) return {};
            if (!root.empty() && root.back() != L'\\') root += L'\\';
            return root + L"Saved\\rollback_lab_request.txt";
        }

        static std::wstring beta_file_path()
        {
            std::wstring root = parent_dir(module_path());
            if (root.empty()) return {};
            if (_wcsicmp(leaf_dir(root).c_str(), L"dlls") == 0)
                root = parent_dir(root);
            if (root.empty()) return {};
            if (!root.empty() && root.back() != L'\\') root += L'\\';
            return root + L"Saved\\rollback_beta.ini";
        }

        static std::string trim_ascii(std::string s)
        {
            auto is_space = [](unsigned char c) {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            };
            while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
                s.pop_back();
            size_t first = 0;
            while (first < s.size()
                   && is_space(static_cast<unsigned char>(s[first])))
                ++first;
            if (first > 0) s.erase(0, first);
            return s;
        }

        static std::string lower_ascii(std::string s)
        {
            for (char& c : s)
            {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + ('a' - 'A'));
            }
            return s;
        }

        static bool parse_bool_string(
            const std::string& value,
            bool fallback)
        {
            const std::string v = lower_ascii(trim_ascii(value));
            if (v == "1" || v == "true" || v == "yes" || v == "on")
                return true;
            if (v == "0" || v == "false" || v == "no" || v == "off")
                return false;
            return fallback;
        }

        static uint32_t parse_u32_ascii(
            const std::string& value,
            uint32_t fallback) noexcept
        {
            if (value.empty()) return fallback;
            char* end = nullptr;
            const unsigned long parsed =
                std::strtoul(value.c_str(), &end, 0);
            if (end == value.c_str()) return fallback;
            return static_cast<uint32_t>(parsed);
        }

        static int32_t parse_i32_ascii(
            const std::string& value,
            int32_t fallback) noexcept
        {
            if (value.empty()) return fallback;
            char* end = nullptr;
            const long parsed =
                std::strtol(value.c_str(), &end, 0);
            if (end == value.c_str()) return fallback;
            return static_cast<int32_t>(parsed);
        }

        static uint64_t parse_u64_ascii(
            const std::string& value,
            uint64_t fallback) noexcept
        {
            if (value.empty()) return fallback;
            char* end = nullptr;
            const unsigned long long parsed =
                std::strtoull(value.c_str(), &end, 0);
            if (end == value.c_str()) return fallback;
            return static_cast<uint64_t>(parsed);
        }

        static bool parse_sha256_ascii(
            const std::string& value,
            std::array<uint8_t, 32>& out) noexcept
        {
            if (value.size() != out.size() * 2u) return false;
            auto nibble = [](char c) noexcept -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            std::array<uint8_t, 32> parsed {};
            for (size_t i = 0; i < parsed.size(); ++i)
            {
                const int high = nibble(value[i * 2u]);
                const int low = nibble(value[i * 2u + 1u]);
                if (high < 0 || low < 0) return false;
                parsed[i] = static_cast<uint8_t>((high << 4) | low);
            }
            out = parsed;
            return true;
        }

        static bool read_text_file(
            const std::wstring& path,
            std::string& out,
            size_t limit = 16384,
            bool* too_large = nullptr)
        {
            out.clear();
            if (too_large) *too_large = false;
            FILE* f = nullptr;
            if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
                return false;
            struct FileGuard final
            {
                FILE* value {nullptr};
                ~FileGuard() noexcept
                {
                    if (value) std::fclose(value);
                }
            } file_guard {f};
            char buf[4096];
            size_t total = 0;
            while (!std::feof(f))
            {
                const size_t remaining =
                    total <= limit ? limit - total : 0;
                const size_t request =
                    (std::min)(sizeof(buf), remaining + 1);
                const size_t n = std::fread(buf, 1, request, f);
                if (n == 0) break;
                out.append(buf, n);
                total += n;
                if (total > limit)
                {
                    out.clear();
                    if (too_large) *too_large = true;
                    return false;
                }
            }
            return !out.empty() && std::ferror(f) == 0;
        }

        bool configure_beta_file() noexcept
        {
            try
            {
                const std::wstring path = beta_file_path();
                if (path.empty()
                    || GetFileAttributesW(path.c_str())
                        == INVALID_FILE_ATTRIBUTES)
                    return false;
                std::string text;
                bool too_large = false;
                if (!read_text_file(path, text, 16384, &too_large))
                {
                    m_controller.reject_configuration_failure(
                        too_large
                            ? "beta-config-too-large"
                            : "beta-config-read-failed");
                    return true;
                }
                RollbackBetaConfig beta {};
                if (!ParseRollbackBetaConfig(text, beta))
                {
                    if (std::strcmp(
                            beta.failure, "beta-config-disabled") == 0)
                    {
                        RollbackLabConfig disabled {};
                        disabled.source = "beta-config-disabled";
                        m_controller.configure(std::move(disabled));
                    }
                    else
                    {
                        m_controller.reject_configuration_failure(
                            beta.failure);
                    }
                    return true;
                }

                RollbackLabConfig cfg {};
                cfg.enabled = true;
                cfg.trace_enabled = beta.trace;
                cfg.test_case = RollbackLabCase::Production;
                cfg.rollback_window = beta.rollback_window;
                cfg.client_role =
                    beta.transport == RollbackBetaTransport::SteamP2P
                    ? "steam" : RollbackBetaRoleName(beta.role);
                cfg.request_id =
                    beta.config_version == kRollbackBetaConfigVersion
                    ? "rollback-beta-v3" : "rollback-beta-v1";
                cfg.request_generation = 1;
                cfg.source = "beta-config";
                cfg.production.enabled = true;
                cfg.production.transport_mode =
                    beta.transport == RollbackBetaTransport::SteamP2P
                    ? RollbackTransportMode::SteamP2P
                    : RollbackTransportMode::DirectUdp;
                cfg.production.bind_address = beta.bind_address;
                cfg.production.bind_port = beta.bind_port;
                cfg.production.peer_address = beta.peer_address;
                cfg.production.peer_port = beta.peer_port;
                if (beta.transport != RollbackBetaTransport::SteamP2P)
                {
                    cfg.production.local_player_slot =
                        beta.local_player_slot();
                    cfg.production.native_input_source_slot =
                        beta.local_player_slot();
                    cfg.production.local_peer = beta.local_peer();
                    cfg.production.remote_peer = beta.remote_peer();
                }
                cfg.production.lifecycle_mode =
                    RollbackLifecycleMode::StockOnlinePvp;
                cfg.production.session_domain =
                    RollbackSessionDomain::Production;
                cfg.production.secret = beta.secret;
                cfg.production.input_delay = beta.input_delay;
                cfg.production.save_policy = beta.save_policy;
                cfg.production.lead_pacing = beta.lead_pacing;
                cfg.production.bind_observed_stock_selection = true;
                cfg.production.network_profile =
                    RollbackNetworkProfileKind::Clean0ms;
                if (cfg.trace_enabled)
                    ReplayDebugTrace::instance().set_enabled(true);
                m_controller.configure(std::move(cfg));
                return true;
            }
            catch (...)
            {
                m_controller.reject_configuration_failure(
                    "beta-config-allocation-failed");
                return true;
            }
        }

        void consume_request_file() noexcept
        {
            try
            {
                consume_request_file_impl();
            }
            catch (...)
            {
                m_controller.reject_configuration_failure(
                    "request-file-config-allocation-failed");
            }
        }

        void consume_request_file_impl()
        {
            const std::wstring path = request_file_path();
            if (path.empty() || GetFileAttributesW(path.c_str())
                    == INVALID_FILE_ATTRIBUTES)
                return;

            std::string text;
            if (!read_text_file(path, text))
                return;

            RollbackLabConfig cfg{};
            cfg.enabled = true;
            cfg.source = "request-file";
            size_t line_start = 0;
            while (line_start <= text.size())
            {
                size_t line_end = text.find('\n', line_start);
                if (line_end == std::string::npos)
                    line_end = text.size();
                std::string line = trim_ascii(
                    text.substr(line_start, line_end - line_start));
                line_start = line_end + 1;
                if (line.empty() || line[0] == '#') continue;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key =
                    lower_ascii(trim_ascii(line.substr(0, eq)));
                const std::string value = trim_ascii(line.substr(eq + 1));
                if (key == "enabled")
                    cfg.enabled = parse_bool_string(value, cfg.enabled);
                else if (key == "trace")
                    cfg.trace_enabled =
                        parse_bool_string(value, cfg.trace_enabled);
                else if (key == "frame_input_log_trace_only")
                    cfg.native_lifecycle_trace_frame_input_log_only =
                        parse_bool_string(
                            value,
                            cfg.native_lifecycle_trace_frame_input_log_only);
                else if (key == "case")
                    cfg.test_case = rollback_case_from_string(value);
                else if (key == "window")
                    cfg.rollback_window =
                        parse_u32_ascii(value, cfg.rollback_window);
                else if (key == "seed")
                    cfg.seed = parse_u32_ascii(value, cfg.seed);
                else if (key == "production_enabled")
                    cfg.production.enabled = parse_bool_string(
                        value, cfg.production.enabled);
                else if (key == "production_transport")
                {
                    const std::string transport = lower_ascii(value);
                    cfg.production.transport_mode =
                        transport == "steam-p2p"
                        ? RollbackTransportMode::SteamP2P
                        : (transport == "direct-udp"
                            ? RollbackTransportMode::DirectUdp
                            : static_cast<RollbackTransportMode>(0xFF));
                }
                else if (key == "diagnostic_logical_release_only")
                    cfg.production_logical_release_only =
                        parse_bool_string(
                            value,
                            cfg.production_logical_release_only);
                else if (key == "replay_fork")
                    cfg.replay_fork_requested = parse_bool_string(
                        value, cfg.replay_fork_requested);
                else if (key == "bind_address")
                    cfg.production.bind_address = value;
                else if (key == "bind_port")
                    cfg.production.bind_port = static_cast<uint16_t>(
                        parse_u32_ascii(value, cfg.production.bind_port));
                else if (key == "peer_address")
                    cfg.production.peer_address = value;
                else if (key == "peer_port")
                    cfg.production.peer_port = static_cast<uint16_t>(
                        parse_u32_ascii(value, cfg.production.peer_port));
                else if (key == "local_player_slot")
                    cfg.production.local_player_slot = static_cast<uint8_t>(
                        parse_u32_ascii(
                            value, cfg.production.local_player_slot));
                else if (key == "native_input_source_slot")
                    cfg.production.native_input_source_slot =
                        static_cast<uint8_t>(parse_u32_ascii(
                            value,
                            cfg.production.native_input_source_slot));
                else if (key == "lifecycle_mode")
                    cfg.production.lifecycle_mode =
                        rollback_lifecycle_mode_from_string(
                            lower_ascii(value));
                else if (key == "bind_observed_stock_selection")
                    cfg.production.bind_observed_stock_selection =
                        parse_bool_string(value,
                            cfg.production.bind_observed_stock_selection);
                else if (key == "replay_test_selection_override")
                    cfg.production.replay_test_selection_override =
                        parse_bool_string(value,
                            cfg.production.replay_test_selection_override);
                else if (key == "launch_left_character")
                    cfg.launch_left_character_override = parse_i32_ascii(
                        value, cfg.launch_left_character_override);
                else if (key == "launch_right_character")
                    cfg.launch_right_character_override = parse_i32_ascii(
                        value, cfg.launch_right_character_override);
                else if (key == "launch_left_character_code")
                    cfg.launch_left_character_code_override = value;
                else if (key == "launch_right_character_code")
                    cfg.launch_right_character_code_override = value;
                else if (key == "launch_stage")
                    cfg.launch_stage_override = parse_i32_ascii(
                        value, cfg.launch_stage_override);
                else if (key == "launch_rounds_to_win")
                    cfg.launch_rounds_to_win_override = parse_i32_ascii(
                        value, cfg.launch_rounds_to_win_override);
                else if (key == "production_local_peer")
                    cfg.production.local_peer = static_cast<uint8_t>(
                        parse_u32_ascii(value, cfg.production.local_peer));
                else if (key == "production_remote_peer")
                    cfg.production.remote_peer = static_cast<uint8_t>(
                        parse_u32_ascii(value, cfg.production.remote_peer));
                else if (key == "secret")
                    cfg.production.secret = value;
                else if (key == "input_delay")
                    cfg.production.input_delay = static_cast<uint16_t>(
                        parse_u32_ascii(value, cfg.production.input_delay));
                else if (key == "save_policy")
                    cfg.production.save_policy = lower_ascii(value)
                            == "confirmed-speculative"
                        ? RollbackSavePolicy::ConfirmedSpeculative
                        : lower_ascii(value) == "every-advance"
                            ? RollbackSavePolicy::EveryAdvance
                            : static_cast<RollbackSavePolicy>(0xFF);
                else if (key == "lead_pacing")
                    cfg.production.lead_pacing.enabled =
                        parse_bool_string(
                            value, cfg.production.lead_pacing.enabled);
                else if (key == "lead_pacing_enter_milliframes")
                {
                    const uint32_t parsed = parse_u32_ascii(value, UINT32_MAX);
                    cfg.production.lead_pacing.enter_frames =
                        parsed <= UINT16_MAX
                        ? RollbackMilliframesToFrames(
                            static_cast<uint16_t>(parsed)) : -1.0f;
                }
                else if (key == "lead_pacing_exit_milliframes")
                {
                    const uint32_t parsed = parse_u32_ascii(value, UINT32_MAX);
                    cfg.production.lead_pacing.exit_frames =
                        parsed <= UINT16_MAX
                        ? RollbackMilliframesToFrames(
                            static_cast<uint16_t>(parsed)) : -1.0f;
                }
                else if (key == "lead_pacing_max_holds")
                    cfg.production.lead_pacing.maximum_consecutive_holds =
                        static_cast<uint8_t>(parse_u32_ascii(
                            value,
                            cfg.production.lead_pacing
                                .maximum_consecutive_holds));
                else if (key == "deterministic_input_enabled")
                    cfg.production.deterministic_input.enabled =
                        parse_bool_string(value,
                            cfg.production.deterministic_input.enabled);
                else if (key == "fixture_id")
                    cfg.production.deterministic_input.fixture_id =
                        parse_u32_ascii(value,
                            cfg.production.deterministic_input.fixture_id);
                else if (key == "correction_start")
                    cfg.production.deterministic_input.correction_start =
                        parse_u32_ascii(value,
                            cfg.production.deterministic_input.correction_start);
                else if (key == "hold_updates")
                    cfg.production.deterministic_input.hold_updates =
                        static_cast<uint16_t>(parse_u32_ascii(value,
                            cfg.production.deterministic_input.hold_updates));
                else if (key == "prediction_lead_updates")
                    cfg.production.deterministic_input
                        .prediction_lead_updates = static_cast<uint16_t>(
                            parse_u32_ascii(value,
                                cfg.production.deterministic_input
                                    .prediction_lead_updates));
                else if (key == "replay_input_enabled")
                    cfg.production.replay_input.enabled = parse_bool_string(
                        value, cfg.production.replay_input.enabled);
                else if (key == "replay_input_file")
                    cfg.replay_input_file = value;
                else if (key == "replay_input_round")
                    cfg.production.replay_input.round_index =
                        parse_u32_ascii(
                            value, cfg.production.replay_input.round_index);
                else if (key == "replay_input_start_frame")
                    cfg.production.replay_input.start_frame =
                        parse_u32_ascii(
                            value, cfg.production.replay_input.start_frame);
                else if (key == "replay_input_random_seed")
                    cfg.production.replay_input.replay_random_seed =
                        parse_u32_ascii(
                            value,
                            cfg.production.replay_input.replay_random_seed);
                else if (key == "replay_input_round_start_frames")
                {
                    auto& replay = cfg.production.replay_input;
                    replay.round_start_frames.fill(0);
                    replay.round_start_count = 0;
                    size_t cursor = 0;
                    while (cursor < value.size()
                        && replay.round_start_count
                            < replay.round_start_frames.size())
                    {
                        const size_t comma = value.find(',', cursor);
                        const std::string token = value.substr(
                            cursor,
                            comma == std::string::npos
                                ? std::string::npos : comma - cursor);
                        if (token.empty())
                        {
                            replay.round_start_count = 0;
                            break;
                        }
                        const uint32_t parsed = parse_u32_ascii(
                            token, UINT32_MAX);
                        if (parsed > UINT16_MAX)
                        {
                            replay.round_start_count = 0;
                            break;
                        }
                        replay.round_start_frames[
                            replay.round_start_count++] =
                            static_cast<uint16_t>(parsed);
                        if (comma == std::string::npos) break;
                        cursor = comma + 1;
                    }
                }
                else if (key == "replay_input_round_frames")
                    cfg.production.replay_input.round_frame_count =
                        parse_u32_ascii(value,
                            cfg.production.replay_input.round_frame_count);
                else if (key == "replay_input_hash_p0")
                    cfg.production.replay_input.round_input_hash[0] =
                        parse_u64_ascii(value,
                            cfg.production.replay_input.round_input_hash[0]);
                else if (key == "replay_input_hash_p1")
                    cfg.production.replay_input.round_input_hash[1] =
                        parse_u64_ascii(value,
                            cfg.production.replay_input.round_input_hash[1]);
                else if (key == "replay_input_swap_players")
                    cfg.production.replay_input.swap_players =
                        parse_bool_string(
                            value, cfg.production.replay_input.swap_players);
                else if (key == "replay_input_exact_delay_alignment")
                    cfg.production.replay_input.alignment =
                        parse_bool_string(value, true)
                        ? RollbackReplayInputAlignment::ExactConsumedFrame
                        : static_cast<RollbackReplayInputAlignment>(0xFF);
                else if (key == "replay_trace_compare")
                    cfg.production.replay_trace_compare =
                        parse_bool_string(
                            value, cfg.production.replay_trace_compare);
                else if (key == "replay_deep_trace_diagnostics")
                    cfg.production.replay_deep_trace_diagnostics =
                        parse_bool_string(
                            value,
                            cfg.production.replay_deep_trace_diagnostics);
                else if (key == "callback_inventory_only")
                    cfg.production.callback_inventory_only =
                        parse_bool_string(
                            value, cfg.production.callback_inventory_only);
                else if (key == "native_correction_only")
                    cfg.production.native_correction_only =
                        parse_bool_string(
                            value, cfg.production.native_correction_only);
                else if (key == "replay_input_sha256")
                {
                    if (!parse_sha256_ascii(
                            value,
                            cfg.production.replay_input.file_sha256))
                        cfg.production.replay_input.file_sha256 = {};
                }
                else if (key == "network_profile")
                {
                    RollbackNetworkProfileKind profile {};
                    cfg.production.network_profile =
                        TryParseRollbackNetworkProfile(
                            lower_ascii(value), profile)
                        ? profile
                        : static_cast<RollbackNetworkProfileKind>(0xFF);
                }
                else if (key == "fault_seed")
                    cfg.production.fault_seed = static_cast<uint32_t>(
                        parse_u64_ascii(value, cfg.production.fault_seed));
                else if (key == "qualification_contract_version")
                    cfg.qualification_contract_version = parse_u32_ascii(
                        value, cfg.qualification_contract_version);
                else if (key == "qualification_run_id")
                    cfg.qualification_run_id = value;
                else if (key == "qualification_case_id")
                    cfg.qualification_case_id = value;
                else if (key == "qualification_segment_id")
                    cfg.qualification_segment_id = value;
                else if (key == "qualification_schedule_hash")
                    cfg.qualification_schedule_hash = value;
                else if (key == "qualification_content_sha256")
                    cfg.qualification_content_sha256 = value;
                else if (key == "qualification_protocol_version")
                    cfg.qualification_protocol_version = parse_u32_ascii(
                        value, cfg.qualification_protocol_version);
                else if (key == "qualification_snapshot_version")
                    cfg.qualification_snapshot_version = parse_u32_ascii(
                        value, cfg.qualification_snapshot_version);
                else if (key == "test_worker_stall_after_ms")
                    cfg.production.test_worker_stall_after_ms =
                        parse_u32_ascii(
                            value,
                            cfg.production.test_worker_stall_after_ms);
                else if (key == "test_worker_stall_duration_ms")
                    cfg.production.test_worker_stall_duration_ms =
                        parse_u32_ascii(
                            value,
                            cfg.production.test_worker_stall_duration_ms);
                else if (key == "test_forced_rollback_depth")
                {
                    const uint32_t parsed = parse_u32_ascii(value, UINT32_MAX);
                    cfg.production.test_forced_rollback_depth =
                        parsed <= UINT8_MAX
                        ? static_cast<uint8_t>(parsed) : UINT8_MAX;
                }
                else if (key == "expected_build_id")
                    cfg.production.expected_build_id = parse_u64_ascii(
                        value, cfg.production.expected_build_id);
                else if (key == "expected_schema_id")
                    cfg.production.expected_schema_id = parse_u64_ascii(
                        value, cfg.production.expected_schema_id);
                else if (key == "replay_sha256")
                {
                    if (!parse_sha256_ascii(
                            value, cfg.production.replay_sha256))
                        cfg.production.replay_sha256 = {};
                }
                else if (key == "replay_anchor_sequence")
                    cfg.production.replay_anchor_sequence = parse_i32_ascii(
                        value, cfg.production.replay_anchor_sequence);
                else if (key == "replay_anchor_round")
                    cfg.production.replay_anchor_round = parse_i32_ascii(
                        value, cfg.production.replay_anchor_round);
                else if (key == "replay_anchor_master")
                    cfg.production.replay_anchor_master = parse_i32_ascii(
                        value, cfg.production.replay_anchor_master);
                else if (key == "replay_run_nonce_hash")
                    cfg.production.replay_run_nonce_hash = parse_u64_ascii(
                        value, cfg.production.replay_run_nonce_hash);
                else if (key == "replay_fork_stability_ticks")
                    cfg.replay_fork_stability_ticks = parse_u32_ascii(
                        value, cfg.replay_fork_stability_ticks);
                else if (key == "replay_fork_run_frames")
                    cfg.replay_fork_run_frames = parse_u32_ascii(
                        value, cfg.replay_fork_run_frames);
                else if (key == "replay_fork_require_rollback")
                    cfg.replay_fork_require_rollback = parse_bool_string(
                        value, cfg.replay_fork_require_rollback);
                else if (key == "activation_arm")
                    cfg.live_activation_operator_enable =
                        parse_bool_string(
                            value, cfg.live_activation_operator_enable);
                else if (key == "activation_source_peer")
                    cfg.live_activation_source_peer = static_cast<uint8_t>(
                        parse_u32_ascii(
                            value, cfg.live_activation_source_peer));
                else if (key == "activation_destination_peer")
                    cfg.live_activation_destination_peer =
                        static_cast<uint8_t>(
                            parse_u32_ascii(
                                value,
                                cfg.live_activation_destination_peer));
                else if (key == "activation_session_id")
                    cfg.live_activation_session_id =
                        parse_u64_ascii(value,
                                        cfg.live_activation_session_id);
                else if (key == "client_role")
                    cfg.client_role = value;
                else if (key == "sandbox_root")
                    cfg.sandbox_root = value;
                else if (key == "sandbox_box")
                    cfg.sandbox_box = value;
                else if (key == "local_peer_id")
                    cfg.local_peer_id = static_cast<uint8_t>(
                        parse_u32_ascii(value, cfg.local_peer_id));
                else if (key == "remote_peer_id")
                    cfg.remote_peer_id = static_cast<uint8_t>(
                        parse_u32_ascii(value, cfg.remote_peer_id));
                else if (key == "sidecar_local_port")
                    cfg.sidecar_local_port = static_cast<uint16_t>(
                        parse_u32_ascii(value, cfg.sidecar_local_port));
                else if (key == "sidecar_remote_port")
                    cfg.sidecar_remote_port = static_cast<uint16_t>(
                        parse_u32_ascii(value, cfg.sidecar_remote_port));
                else if (key == "sidecar_remote_addr")
                    cfg.sidecar_remote_addr = value;
                else if (key == "activation_token")
                    cfg.activation_token = value;
                else if (key == "observe_gameflow")
                    cfg.observe_gameflow_requested =
                        parse_bool_string(
                            value, cfg.observe_gameflow_requested);
                else if (key == "observe_gameflow_process_events")
                    cfg.observe_gameflow_process_events =
                        parse_bool_string(
                            value,
                            cfg.observe_gameflow_process_events);
                else if (key == "online_stage")
                    cfg.online_stage_requested =
                        parse_bool_string(value, cfg.online_stage_requested);
                else if (key == "online_stage_cleanup_only")
                    cfg.online_stage_cleanup_only =
                        parse_bool_string(
                            value, cfg.online_stage_cleanup_only);
                else if (key == "online_stage_wait_host_room_ready_marker")
                    cfg.online_stage_wait_host_room_ready_marker =
                        parse_bool_string(
                            value,
                            cfg.online_stage_wait_host_room_ready_marker);
                else if (key == "online_stage_main_user_id_override")
                    cfg.online_stage_main_user_id_override =
                        parse_i32_ascii(
                            value,
                            cfg.online_stage_main_user_id_override);
                else if (key == "online_stage_host_room_ready_marker")
                    cfg.online_stage_host_room_ready_marker = value;
                else if (key == "online_stage_goal")
                    cfg.online_stage_goal = value;
                else if (key == "main_menu_player_match_route")
                    cfg.main_menu_player_match_route = value;
                else if (key == "local_replay_player")
                    cfg.local_replay_player = static_cast<uint8_t>(
                        parse_u32_ascii(value, cfg.local_replay_player));
                else if (key == "remote_replay_player")
                    cfg.remote_replay_player = static_cast<uint8_t>(
                        parse_u32_ascii(value, cfg.remote_replay_player));
                else if (key == "replay_divergence_frame")
                    cfg.replay_divergence_frame =
                        parse_u32_ascii(value, cfg.replay_divergence_frame);
                else if (key == "replay_divergence_window")
                    cfg.replay_divergence_window =
                        parse_u32_ascii(value, cfg.replay_divergence_window);
                else if (key == "output")
                    cfg.output_path = value;
                else if (key == "request_id")
                    cfg.request_id = value;
                else if (key == "request_generation")
                    cfg.request_generation = parse_u64_ascii(
                        value, cfg.request_generation);
            }

            if (cfg.replay_fork_requested)
            {
                cfg.enabled = true;
                cfg.production.enabled = true;
                cfg.production.session_domain =
                    RollbackSessionDomain::ReplayForkLab;
                cfg.test_case = RollbackLabCase::ReplayForkLab;
            }
            else if (cfg.production.enabled)
            {
                cfg.enabled = true;
                cfg.test_case = RollbackLabCase::Production;
            }
            DeleteFileW(path.c_str());
            if (cfg.trace_enabled)
                ReplayDebugTrace::instance().set_enabled(true);
            m_controller.configure(std::move(cfg));
        }

        static bool has_flag(const wchar_t* option)
        {
            const std::wstring wanted(option ? option : L"");
            for (const std::wstring& a : argv())
            {
                if (_wcsicmp(a.c_str(), wanted.c_str()) == 0)
                    return true;
            }
            return false;
        }

        static bool option_value(const wchar_t* option,
                                 std::wstring& out)
        {
            const std::wstring wanted(option ? option : L"");
            const std::wstring prefix = wanted + L"=";
            const auto args = argv();
            for (size_t i = 0; i < args.size(); ++i)
            {
                const std::wstring& a = args[i];
                if (_wcsicmp(a.c_str(), wanted.c_str()) == 0)
                {
                    if (i + 1 < args.size())
                    {
                        out = args[i + 1];
                        return !out.empty();
                    }
                    return false;
                }
                if (_wcsnicmp(a.c_str(), prefix.c_str(), prefix.size()) == 0)
                {
                    out = a.substr(prefix.size());
                    return !out.empty();
                }
            }
            return false;
        }

        static uint32_t parse_u32(const std::wstring& value,
                                  uint32_t fallback) noexcept
        {
            if (value.empty()) return fallback;
            wchar_t* end = nullptr;
            const unsigned long parsed =
                std::wcstoul(value.c_str(), &end, 0);
            if (end == value.c_str()) return fallback;
            return static_cast<uint32_t>(parsed);
        }

        static int32_t parse_i32(const std::wstring& value,
                                 int32_t fallback) noexcept
        {
            if (value.empty()) return fallback;
            wchar_t* end = nullptr;
            const long parsed =
                std::wcstol(value.c_str(), &end, 0);
            if (end == value.c_str()) return fallback;
            return static_cast<int32_t>(parsed);
        }

        static uint64_t parse_u64(const std::wstring& value,
                                  uint64_t fallback) noexcept
        {
            if (value.empty()) return fallback;
            wchar_t* end = nullptr;
            const unsigned long long parsed =
                std::wcstoull(value.c_str(), &end, 0);
            if (end == value.c_str()) return fallback;
            return static_cast<uint64_t>(parsed);
        }

        static std::string wide_to_utf8(const std::wstring& in)
        {
            if (in.empty()) return {};
            const int need = WideCharToMultiByte(
                CP_UTF8, 0, in.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (need <= 1) return {};
            std::string out(static_cast<size_t>(need - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, out.data(), need,
                                nullptr, nullptr);
            return out;
        }

        RollbackController m_controller {};
        std::atomic<bool> m_cmd_consumed {false};
        std::atomic<int> m_pending_ui_enable {-1};
        std::atomic<bool> m_ui_enabled {false};
        std::atomic<uint32_t> m_ui_test_case {
            static_cast<uint32_t>(RollbackLabCase::BaselineOracle)};
        std::atomic<uint32_t> m_ui_rollback_window {12};
        std::atomic<uint32_t> m_ui_seed {0x5C6B0001u};
        std::atomic<uint64_t> m_ui_service_ticks {0};
        std::atomic<size_t> m_ui_manifest_entries {0};
        std::atomic<uint64_t> m_ui_manifest_hash {0};
    };

    inline void RollbackDiag::emit_configured(
        const RollbackLabConfig& cfg,
        const RollbackSnapshotManifest* manifest) noexcept
    {
        try
        {
        ReplayTraceFields f;
        f.boolean("enabled", cfg.enabled)
         .boolean("trace_enabled", cfg.trace_enabled)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .string("network_profile",
                 RollbackNetworkProfileName(
                     cfg.production.network_profile))
         .boolean("callback_inventory_only",
                  cfg.production.callback_inventory_only)
         .boolean("native_correction_only",
                  cfg.production.native_correction_only)
         .boolean("diagnostic_logical_release_only",
                  cfg.production_logical_release_only)
         .hex("fault_seed", cfg.production.fault_seed)
         .boolean("live_activation_operator_enable",
                  cfg.live_activation_operator_enable)
         .uinteger("live_activation_source_peer",
                   static_cast<uint64_t>(
                       cfg.live_activation_source_peer))
         .uinteger("live_activation_destination_peer",
                   static_cast<uint64_t>(
                       cfg.live_activation_destination_peer))
         .hex("live_activation_session_id",
              cfg.live_activation_session_id)
         .string("client_role", cfg.client_role)
         .string("sandbox_root", cfg.sandbox_root)
         .string("sandbox_box", cfg.sandbox_box)
         .uinteger("local_peer_id",
                   static_cast<uint64_t>(cfg.local_peer_id))
         .uinteger("remote_peer_id",
                   static_cast<uint64_t>(cfg.remote_peer_id))
         .uinteger("sidecar_local_port",
                   static_cast<uint64_t>(cfg.sidecar_local_port))
         .uinteger("sidecar_remote_port",
                   static_cast<uint64_t>(cfg.sidecar_remote_port))
         .string("sidecar_remote_addr", cfg.sidecar_remote_addr)
         .hex("activation_token_hash",
              RollbackSidecarTokenHash(cfg.activation_token))
         .boolean("observe_gameflow_requested",
                  cfg.observe_gameflow_requested)
         .boolean("observe_gameflow_process_events",
                  cfg.observe_gameflow_process_events)
         .boolean("online_stage_requested", cfg.online_stage_requested)
         .boolean("online_stage_cleanup_only",
                  cfg.online_stage_cleanup_only)
         .boolean("online_stage_wait_host_room_ready_marker",
                  cfg.online_stage_wait_host_room_ready_marker)
         .integer("online_stage_main_user_id_override",
                  cfg.online_stage_main_user_id_override)
         .string("stock_join_route",
                 RollbackStockJoinRouteName(cfg.stock_join_route))
         .string("online_stage_host_room_ready_marker",
                 cfg.online_stage_host_room_ready_marker)
         .string("online_stage_goal", cfg.online_stage_goal)
         .string("main_menu_player_match_route",
                 cfg.main_menu_player_match_route)
         .uinteger("local_replay_player",
                   static_cast<uint64_t>(cfg.local_replay_player))
         .uinteger("remote_replay_player",
                   static_cast<uint64_t>(cfg.remote_replay_player))
         .uinteger("replay_divergence_frame",
                   static_cast<uint64_t>(cfg.replay_divergence_frame))
         .uinteger("replay_divergence_window",
                   static_cast<uint64_t>(cfg.replay_divergence_window))
         .string("request_id", cfg.request_id)
         .hex("request_generation", cfg.request_generation)
         .string("source", cfg.source)
         .string("output_path", cfg.output_path)
         .string("mod_root",
                 ReplayDebugTrace::instance().current_mod_root_utf8())
         .string("trace_path",
                 ReplayDebugTrace::instance().current_path_utf8());
        if (manifest)
        {
            f.uinteger("manifest_entries", manifest->entries.size())
             .hex("manifest_image_base", manifest->image_base)
             .hex("manifest_hash", manifest->coverage_hash())
             .hex("manifest_epoch_chara_p1", manifest->epoch.chara[0])
             .hex("manifest_epoch_chara_p2", manifest->epoch.chara[1])
             .uinteger("manifest_epoch_presence",
                       static_cast<uint64_t>(manifest->epoch.presence));
        }
        ReplayDebugTrace::instance().event("rollback_lab_configured", f);

        if (!cfg.enabled && cfg.source == "default-disabled")
        {
            RC::Output::send<RC::LogLevel::Verbose>(STR(
                "[RollbackLab] configured disabled by default\n"));
            return;
        }

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] configured enabled={} case={} window={} "
            "seed=0x{:08X} source={}\n"),
            cfg.enabled ? 1 : 0,
            RC::to_generic_string(std::string(
                rollback_case_name(cfg.test_case))),
            cfg.rollback_window,
            cfg.seed,
            RC::to_generic_string(cfg.source));
        }
        catch (...)
        {
            // Diagnostics are best-effort and cannot affect activation.
        }
    }

    inline void RollbackDiag::emit_service_tick(
        uint64_t tick,
        const RollbackLabConfig& cfg) noexcept
    {
        try
        {
        ReplayTraceFields f;
        f.uinteger("service_tick", tick)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed);
        ReplayDebugTrace::instance().event("rollback_lab_service_tick", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] service_tick={} case={} window={} seed=0x{:08X}\n"),
            tick,
            RC::to_generic_string(std::string(
                rollback_case_name(cfg.test_case))),
            cfg.rollback_window,
            cfg.seed);
        }
        catch (...)
        {
            // Diagnostics are best-effort and cannot affect simulation.
        }
    }

    inline void RollbackDiag::emit_two_client_role_manifest(
        const RollbackLabConfig& cfg,
        const RollbackSnapshotManifest* manifest) noexcept
    {
        try
        {
        ReplayTraceFields f;
        f.string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .string("source", cfg.source)
         .string("client_role", cfg.client_role)
         .string("sandbox_root", cfg.sandbox_root)
         .string("sandbox_box", cfg.sandbox_box)
         .uinteger("local_peer_id",
                   static_cast<uint64_t>(cfg.local_peer_id))
         .uinteger("remote_peer_id",
                   static_cast<uint64_t>(cfg.remote_peer_id))
         .hex("session_id", cfg.live_activation_session_id)
         .string("network_profile",
                 RollbackNetworkProfileName(
                     cfg.production.network_profile))
         .hex("fault_seed", cfg.production.fault_seed)
         .uinteger("sidecar_local_port",
                   static_cast<uint64_t>(cfg.sidecar_local_port))
         .uinteger("sidecar_remote_port",
                   static_cast<uint64_t>(cfg.sidecar_remote_port))
         .string("sidecar_remote_addr", cfg.sidecar_remote_addr)
         .hex("activation_token_hash",
              RollbackSidecarTokenHash(cfg.activation_token))
         .boolean("sidecar_requested", cfg.sidecar_requested())
         .boolean("observe_gameflow_requested",
                  cfg.observe_gameflow_requested)
         .boolean("observe_gameflow_process_events",
                  cfg.observe_gameflow_process_events)
         .boolean("online_stage_requested", cfg.online_stage_requested)
         .boolean("online_stage_cleanup_only",
                  cfg.online_stage_cleanup_only)
         .boolean("online_stage_wait_host_room_ready_marker",
                  cfg.online_stage_wait_host_room_ready_marker)
         .integer("online_stage_main_user_id_override",
                  cfg.online_stage_main_user_id_override)
         .string("stock_join_route",
                 RollbackStockJoinRouteName(cfg.stock_join_route))
         .string("online_stage_host_room_ready_marker",
                 cfg.online_stage_host_room_ready_marker)
         .string("online_stage_goal", cfg.online_stage_goal)
         .uinteger("local_replay_player",
                   static_cast<uint64_t>(cfg.local_replay_player))
         .uinteger("remote_replay_player",
                   static_cast<uint64_t>(cfg.remote_replay_player))
         .string("mod_root",
                 ReplayDebugTrace::instance().current_mod_root_utf8())
         .string("trace_path",
                 ReplayDebugTrace::instance().current_path_utf8());
        if (manifest)
        {
            f.hex("manifest_image_base", manifest->image_base)
             .hex("manifest_hash", manifest->coverage_hash())
             .hex("manifest_epoch_chara_p1", manifest->epoch.chara[0])
             .hex("manifest_epoch_chara_p2", manifest->epoch.chara[1])
             .uinteger("manifest_epoch_presence",
                       static_cast<uint64_t>(manifest->epoch.presence));
        }
        ReplayDebugTrace::instance().event(
            "rollback_two_client_role_manifest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] role_manifest role={} local_peer={} remote_peer={} "
            "local_port={} remote_port={} request_id={}\n"),
            RC::to_generic_string(cfg.client_role),
            static_cast<uint32_t>(cfg.local_peer_id),
            static_cast<uint32_t>(cfg.remote_peer_id),
            static_cast<uint32_t>(cfg.sidecar_local_port),
            static_cast<uint32_t>(cfg.sidecar_remote_port),
            RC::to_generic_string(cfg.request_id));
        }
        catch (...)
        {
            // Diagnostics are best-effort and cannot affect activation.
        }
    }

    inline void RollbackDiag::emit_snapshot_roundtrip(
        const RollbackSnapshotRoundTripReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .boolean("hash_match", report.hash_match)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .hex("before_hash", report.before_hash)
         .hex("after_hash", report.after_hash)
         .boolean("capture_ok", report.capture.ok)
         .boolean("restore_ok", report.restore.ok)
         .boolean("recapture_ok", report.recapture.ok)
         .uinteger("captured_entries", report.capture.copied_entries)
         .uinteger("captured_bytes", report.capture.copied_bytes)
         .uinteger("skipped_entries", report.capture.skipped_entries)
         .string("capture_failure", report.capture.failure)
         .string("restore_failure", report.restore.failure)
         .string("recapture_failure", report.recapture.failure)
         .uinteger("failed_entry", report.capture.failed_entry)
         .hex("failed_address", report.capture.failed_address);
        ReplayDebugTrace::instance().event(
            "rollback_snapshot_roundtrip", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] snapshot_roundtrip ok={} hash_match={} "
            "entries={} bytes={} before=0x{:X} after=0x{:X} "
            "capture={} restore={} recapture={} failure={}/"
            "{}/{} failed_entry={} failed_addr=0x{:X}\n"),
            report.ok ? 1 : 0,
            report.hash_match ? 1 : 0,
            report.capture.copied_entries,
            report.capture.copied_bytes,
            static_cast<unsigned long long>(report.before_hash),
            static_cast<unsigned long long>(report.after_hash),
            report.capture.ok ? 1 : 0,
            report.restore.ok ? 1 : 0,
            report.recapture.ok ? 1 : 0,
            RC::to_generic_string(std::string(
                report.capture.failure ? report.capture.failure : "?")),
            RC::to_generic_string(std::string(
                report.restore.failure ? report.restore.failure : "?")),
            RC::to_generic_string(std::string(
                report.recapture.failure ? report.recapture.failure : "?")),
            report.capture.failed_entry,
            static_cast<unsigned long long>(report.capture.failed_address));
    }

    inline void RollbackDiag::emit_hgcpu_roundtrip(
        const RollbackHgCpuRoundTripReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .boolean("hash_match", report.hash_match)
         .boolean("policy_match", report.policy_match)
         .boolean("topology_match", report.topology_match)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .hex("before_hash", report.before_hash)
         .hex("after_hash", report.after_hash)
         .hex("before_topology_hash", report.before_topology_hash)
         .hex("after_topology_hash", report.after_topology_hash)
         .boolean("capture_ok", report.capture.ok)
         .boolean("restore_ok", report.restore.ok)
         .boolean("recapture_ok", report.recapture.ok)
         .boolean("context_ready", report.capture.context_ready)
         .uinteger("capacity", report.capture.capacity)
         .uinteger("capture_cursor", report.capture.cursor)
         .uinteger("restore_cursor", report.restore.cursor)
         .uinteger("recapture_cursor", report.recapture.cursor)
         .uinteger("bytes_compared", report.bytes_compared)
         .uinteger("mismatch_count", report.mismatch_count)
         .uinteger("ignored_mismatch_count", report.ignored_mismatch_count)
         .uinteger("unignored_mismatch_count",
                   report.unignored_mismatch_count)
         .hex("first_mismatch_offset", report.first_mismatch_offset)
         .hex("first_mismatch_before", report.first_mismatch_before)
         .hex("first_mismatch_after", report.first_mismatch_after)
         .hex("first_unignored_mismatch_offset",
              report.first_unignored_mismatch_offset)
         .hex("first_unignored_mismatch_before",
              report.first_unignored_mismatch_before)
         .hex("first_unignored_mismatch_after",
              report.first_unignored_mismatch_after)
         .string("first_ignored_reason", report.first_ignored_reason)
         .hex("image_base", report.capture.image_base)
         .hex("capture_function", report.capture.function_address)
         .hex("restore_function", report.restore.function_address)
         .hex("chara_p1", report.capture.chara_p1)
         .hex("chara_p2", report.capture.chara_p2)
         .string("capture_failure", report.capture.failure)
         .string("restore_failure", report.restore.failure)
         .string("recapture_failure", report.recapture.failure)
         .boolean("capture_faulted", report.capture.fault.faulted)
         .hex("capture_exception_code", report.capture.fault.exception_code)
         .hex("capture_exception_rip", report.capture.fault.exception_address)
         .boolean("restore_faulted", report.restore.fault.faulted)
         .hex("restore_exception_code", report.restore.fault.exception_code)
         .hex("restore_exception_rip", report.restore.fault.exception_address);
        ReplayDebugTrace::instance().event(
            "rollback_hgcpu_roundtrip", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] hgcpu_roundtrip ok={} hash_match={} policy={} topology={} "
            "context={} cap={} cursor={}/{}/{} before=0x{:X} after=0x{:X} "
            "mismatches={} ignored={} unignored={} "
            "first=0x{:X}:0x{:02X}->0x{:02X} "
            "first_unignored=0x{:X}:0x{:02X}->0x{:02X} "
            "capture={} restore={} recapture={} failure={}/{}/{} "
            "p1=0x{:X} p2=0x{:X}\n"),
            report.ok ? 1 : 0,
            report.hash_match ? 1 : 0,
            report.policy_match ? 1 : 0,
            report.topology_match ? 1 : 0,
            report.capture.context_ready ? 1 : 0,
            static_cast<unsigned long long>(report.capture.capacity),
            static_cast<unsigned long long>(report.capture.cursor),
            static_cast<unsigned long long>(report.restore.cursor),
            static_cast<unsigned long long>(report.recapture.cursor),
            static_cast<unsigned long long>(report.before_hash),
            static_cast<unsigned long long>(report.after_hash),
            static_cast<unsigned long long>(report.mismatch_count),
            static_cast<unsigned long long>(report.ignored_mismatch_count),
            static_cast<unsigned long long>(
                report.unignored_mismatch_count),
            static_cast<unsigned long long>(report.first_mismatch_offset),
            report.first_mismatch_before,
            report.first_mismatch_after,
            static_cast<unsigned long long>(
                report.first_unignored_mismatch_offset),
            report.first_unignored_mismatch_before,
            report.first_unignored_mismatch_after,
            report.capture.ok ? 1 : 0,
            report.restore.ok ? 1 : 0,
            report.recapture.ok ? 1 : 0,
            RC::to_generic_string(std::string(
                report.capture.failure ? report.capture.failure : "?")),
            RC::to_generic_string(std::string(
                report.restore.failure ? report.restore.failure : "?")),
            RC::to_generic_string(std::string(
                report.recapture.failure ? report.recapture.failure : "?")),
            static_cast<unsigned long long>(report.capture.chara_p1),
            static_cast<unsigned long long>(report.capture.chara_p2));
    }

    inline void RollbackDiag::emit_resim_window(
        const RollbackResimWindowReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        auto append_step_report = [](
            ReplayTraceFields& f,
            const char* prefix,
            const RollbackStepStateReport& step) noexcept
        {
            std::string key(prefix ? prefix : "step");
            f.boolean((key + "_ok").c_str(), step.ok)
             .boolean((key + "_explicit_ok").c_str(), step.explicit_ok)
             .boolean((key + "_hgcpu_ok").c_str(), step.hgcpu_ok)
             .string((key + "_failure").c_str(), step.failure)
             .string((key + "_hgcpu_failure").c_str(),
                     step.hgcpu_report.failure)
             .boolean((key + "_hgcpu_context_ready").c_str(),
                      step.hgcpu_report.context_ready)
             .boolean((key + "_hgcpu_faulted").c_str(),
                      step.hgcpu_report.fault.faulted)
             .hex((key + "_hgcpu_exception_code").c_str(),
                  step.hgcpu_report.fault.exception_code)
             .hex((key + "_hgcpu_exception_rip").c_str(),
                  step.hgcpu_report.fault.exception_address)
             .hex((key + "_hgcpu_function").c_str(),
                  step.hgcpu_report.function_address)
             .hex((key + "_hgcpu_chara_p1").c_str(),
                  step.hgcpu_report.chara_p1)
             .hex((key + "_hgcpu_chara_p2").c_str(),
                  step.hgcpu_report.chara_p2)
             .uinteger((key + "_hgcpu_cursor").c_str(),
                       step.hgcpu_report.cursor)
             .uinteger((key + "_hgcpu_capacity").c_str(),
                       step.hgcpu_report.capacity);
        };

        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .boolean("context_ready", report.context_ready)
         .boolean("inject_fault", report.inject_fault)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", report.window)
         .hex("seed", report.seed)
         .uinteger("start_frame", report.start_frame)
         .uinteger("baseline_frame", report.baseline_frame)
         .uinteger("predicted_frame", report.predicted_frame)
         .uinteger("corrected_frame", report.corrected_frame)
         .uinteger("fault_frame_index", report.fault_frame_index)
         .hex("baseline_hash", report.baseline_hash)
         .hex("predicted_hash", report.predicted_hash)
         .hex("corrected_hash", report.corrected_hash)
         .hex("baseline_explicit_hash", report.baseline_explicit_hash)
         .hex("predicted_explicit_hash", report.predicted_explicit_hash)
         .hex("corrected_explicit_hash", report.corrected_explicit_hash)
         .hex("post_baseline_restore_explicit_hash",
              report.post_baseline_restore_explicit_hash)
         .hex("post_predicted_restore_explicit_hash",
              report.post_predicted_restore_explicit_hash)
         .boolean("post_baseline_restore_explicit_ok",
                  report.post_baseline_restore_explicit_ok)
         .boolean("post_baseline_restore_explicit_match",
                  report.post_baseline_restore_explicit_match)
         .boolean("post_predicted_restore_explicit_ok",
                  report.post_predicted_restore_explicit_ok)
         .boolean("post_predicted_restore_explicit_match",
                  report.post_predicted_restore_explicit_match)
         .boolean("start_lfsr_index_ok", report.start_lfsr_index_ok)
         .boolean("baseline_lfsr_index_ok",
                  report.baseline_lfsr_index_ok)
         .boolean("predicted_lfsr_index_ok",
                  report.predicted_lfsr_index_ok)
         .boolean("corrected_lfsr_index_ok",
                  report.corrected_lfsr_index_ok)
         .boolean("post_baseline_restore_lfsr_index_ok",
                  report.post_baseline_restore_lfsr_index_ok)
         .boolean("post_predicted_restore_lfsr_index_ok",
                  report.post_predicted_restore_lfsr_index_ok)
         .boolean("wind_rng_gate_resolved", report.wind_rng_gate_resolved)
         .boolean("wind_rng_gate_enabled", report.wind_rng_gate_enabled)
         .uinteger("start_lfsr_index", report.start_lfsr_index)
         .uinteger("baseline_lfsr_index", report.baseline_lfsr_index)
         .uinteger("predicted_lfsr_index", report.predicted_lfsr_index)
         .uinteger("corrected_lfsr_index", report.corrected_lfsr_index)
         .uinteger("post_baseline_restore_lfsr_index",
                   report.post_baseline_restore_lfsr_index)
         .uinteger("post_predicted_restore_lfsr_index",
                   report.post_predicted_restore_lfsr_index)
         .hex("baseline_input_p2", report.baseline_input_p2)
         .hex("predicted_input_p2", report.predicted_input_p2)
         .boolean("baseline_ok", report.baseline_ok)
         .boolean("predicted_ok", report.predicted_ok)
         .boolean("corrected_ok", report.corrected_ok)
         .boolean("restore_start_after_ok", report.restore_start_after_ok)
         .boolean("corrected_matches_baseline",
                  report.corrected_matches_baseline)
         .boolean("predicted_differs_from_baseline",
                  report.predicted_differs_from_baseline)
         .boolean("explicit_match", report.explicit_match)
         .boolean("hgcpu_policy_match", report.hgcpu_policy_match)
         .boolean("hgcpu_topology_match",
                  report.corrected_compare.topology_match)
         .boolean("hgcpu_motion_bank_control_match",
                  report.corrected_compare.motion_bank_match)
         .boolean("hgcpu_motion_bank_match",
                  report.corrected_compare.motion_bank_match)
         .boolean("hgcpu_motion_tail_match",
                  report.corrected_compare.motion_tail_match)
         .boolean("hgcpu_timer_node_match",
                  report.corrected_compare.timer_node_match)
         .boolean("frame_counter_match", report.frame_counter_match)
         .boolean("frame_counter_delta_ok", report.frame_counter_delta_ok)
         .boolean("all_steps_ok", report.all_steps_ok)
         .uinteger("steps_attempted", report.steps_attempted)
         .uinteger("steps_ok", report.steps_ok)
         .uinteger("hgcpu_mismatch_count",
                   report.corrected_compare.mismatch_count)
         .uinteger("hgcpu_ignored_mismatch_count",
                   report.corrected_compare.ignored_mismatch_count)
         .uinteger("hgcpu_unignored_mismatch_count",
                   report.corrected_compare.unignored_mismatch_count)
         .hex("baseline_hgcpu_topology_hash",
              report.corrected_compare.topology_hash_a)
         .hex("corrected_hgcpu_topology_hash",
              report.corrected_compare.topology_hash_b)
         .hex("baseline_hgcpu_motion_bank_hash",
              report.corrected_compare.motion_bank_hash_a)
         .hex("corrected_hgcpu_motion_bank_hash",
              report.corrected_compare.motion_bank_hash_b)
         .hex("baseline_hgcpu_motion_tail_hash",
              report.corrected_compare.motion_tail_hash_a)
         .hex("corrected_hgcpu_motion_tail_hash",
              report.corrected_compare.motion_tail_hash_b)
         .hex("baseline_hgcpu_timer_node_hash",
              report.corrected_compare.timer_node_hash_a)
         .hex("corrected_hgcpu_timer_node_hash",
              report.corrected_compare.timer_node_hash_b)
         .uinteger("hgcpu_timer_indexed_nonzero_count",
                   report.corrected_compare.timer_indexed_nonzero_count_a)
         .uinteger("hgcpu_timer_indexed_captured_count",
                   report.corrected_compare.timer_indexed_captured_count_a)
         .uinteger("hgcpu_timer_indexed_object_captured_count",
                   report.corrected_compare
                       .timer_indexed_object_captured_count_a)
         .hex("hgcpu_timer_slot0_root",
              report.corrected_compare.timer_indexed_slot0_root_a)
         .hex("hgcpu_timer_slot0_vtable",
              report.corrected_compare.timer_indexed_slot0_vtable_a)
         .hex("hgcpu_timer_slot0_writer",
              report.corrected_compare.timer_indexed_slot0_writer_a)
         .boolean("hgcpu_timer_slot0_captured",
                  report.corrected_compare.timer_indexed_slot0_captured_a)
         .hex("baseline_hgcpu_p1_record_bytes",
              report.corrected_compare.p1_record_bytes_a)
         .hex("corrected_hgcpu_p1_record_bytes",
              report.corrected_compare.p1_record_bytes_b)
         .hex("baseline_hgcpu_p2_record_bytes",
              report.corrected_compare.p2_record_bytes_a)
         .hex("corrected_hgcpu_p2_record_bytes",
              report.corrected_compare.p2_record_bytes_b)
         .hex("baseline_hgcpu_p2_base",
              report.corrected_compare.p2_base_a)
         .hex("corrected_hgcpu_p2_base",
              report.corrected_compare.p2_base_b)
         .uinteger("hgcpu_motion_bank_mismatch_count",
                   report.corrected_compare.motion_bank_mismatch_count)
         .string("hgcpu_motion_bank_first_region",
                 report.corrected_compare.motion_bank_first_region)
         .uinteger("hgcpu_motion_bank_first_player",
                   report.corrected_compare.motion_bank_first_player)
         .uinteger("hgcpu_motion_bank_first_bank",
                   report.corrected_compare.motion_bank_first_bank)
         .integer("hgcpu_motion_bank_first_buffer",
                  report.corrected_compare.motion_bank_first_buffer)
         .hex("hgcpu_motion_bank_first_offset",
              report.corrected_compare.motion_bank_first_offset)
         .hex("hgcpu_motion_bank_first_a",
              report.corrected_compare.motion_bank_first_a)
         .hex("hgcpu_motion_bank_first_b",
              report.corrected_compare.motion_bank_first_b)
         .integer("hgcpu_motion_bank_first_slot_a",
                  report.corrected_compare.motion_bank_first_slot_a)
         .integer("hgcpu_motion_bank_first_slot_b",
                  report.corrected_compare.motion_bank_first_slot_b)
         .hex("first_hgcpu_mismatch_offset",
              report.corrected_compare.first_mismatch_offset)
         .hex("first_hgcpu_mismatch_a",
              report.corrected_compare.first_mismatch_a)
         .hex("first_hgcpu_mismatch_b",
              report.corrected_compare.first_mismatch_b)
         .hex("first_hgcpu_unignored_offset",
              report.corrected_compare.first_unignored_mismatch_offset)
         .hex("first_hgcpu_unignored_a",
              report.corrected_compare.first_unignored_mismatch_a)
         .hex("first_hgcpu_unignored_b",
              report.corrected_compare.first_unignored_mismatch_b)
         .string("first_khit_region",
                 report.corrected_compare.khit_first_region)
         .integer("first_unignored_dynamic_player",
                  report.corrected_compare.first_unignored_dynamic_player)
         .hex("first_unignored_dynamic_local",
              report.corrected_compare.first_unignored_dynamic_local)
         .uinteger("first_khit_player",
                   report.corrected_compare.khit_first_player)
         .integer("first_khit_list",
                  report.corrected_compare.khit_first_list)
         .integer("first_khit_node_index",
                  report.corrected_compare.khit_first_node_index)
         .hex("first_khit_stream_start",
              report.corrected_compare.khit_first_stream_start)
         .hex("first_khit_stream_rel",
              report.corrected_compare.khit_first_stream_rel)
         .uinteger("first_khit_stream_size",
                   report.corrected_compare.khit_first_stream_size)
         .hex("first_khit_node",
              report.corrected_compare.khit_first_node)
         .hex("first_khit_tag",
              report.corrected_compare.khit_first_tag)
         .hex("first_khit_node_source_offset",
              report.corrected_compare.khit_first_node_source_offset)
         .hex("first_khit_node_source_a",
              report.corrected_compare.khit_first_node_source_a)
         .hex("first_khit_node_source_b",
              report.corrected_compare.khit_first_node_source_b)
         .boolean("first_khit_node_source_match",
                  report.corrected_compare.khit_first_node_source_match)
         .hex("first_khit_node_stream_end",
              report.corrected_compare.khit_first_node_stream_end)
         .hex("first_khit_relocation_start",
              report.corrected_compare.khit_first_relocation_start)
         .string("first_ignored_reason",
                 report.corrected_compare.first_ignored_reason)
         .string("failure", report.failure)
          .string("start_capture_failure", report.start_capture.failure)
          .string("baseline_capture_failure", report.baseline_capture.failure)
          .string("predicted_capture_failure", report.predicted_capture.failure)
          .string("corrected_capture_failure",
                  report.corrected_capture.failure);
        append_step_report(f, "start_capture", report.start_capture);
        append_step_report(f, "baseline_capture", report.baseline_capture);
        append_step_report(f, "predicted_capture", report.predicted_capture);
        append_step_report(f, "corrected_capture", report.corrected_capture);
        append_step_report(
            f, "post_baseline_restore", report.post_baseline_restore);
        append_step_report(
            f, "post_predicted_restore", report.post_predicted_restore);
        append_step_report(f, "final_restore", report.final_restore);
        ReplayDebugTrace::instance().event("rollback_resim_window", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] resim_window ok={} case={} fault={} "
            "window={} seed=0x{:08X} frames={}->{}/{} steps={}/{} "
            "baseline=0x{:X} predicted=0x{:X} corrected=0x{:X} "
            "match={} explicit={} hgcpu_policy={} topology={} "
            "motion_bank_control={} motion_tail={} frame={} "
            "predicted_diff={} mismatches={} unignored={} "
            "failure={}\n"),
            report.ok ? 1 : 0,
            RC::to_generic_string(std::string(
                rollback_case_name(cfg.test_case))),
            report.inject_fault ? 1 : 0,
            report.window,
            report.seed,
            report.start_frame,
            report.baseline_frame,
            report.corrected_frame,
            report.steps_ok,
            report.steps_attempted,
            static_cast<unsigned long long>(report.baseline_hash),
            static_cast<unsigned long long>(report.predicted_hash),
            static_cast<unsigned long long>(report.corrected_hash),
            report.corrected_matches_baseline ? 1 : 0,
            report.explicit_match ? 1 : 0,
            report.hgcpu_policy_match ? 1 : 0,
            report.corrected_compare.topology_match ? 1 : 0,
            report.corrected_compare.motion_bank_match ? 1 : 0,
            report.corrected_compare.motion_tail_match ? 1 : 0,
            report.frame_counter_delta_ok ? 1 : 0,
            report.predicted_differs_from_baseline ? 1 : 0,
            static_cast<unsigned long long>(
                report.corrected_compare.mismatch_count),
            static_cast<unsigned long long>(
                report.corrected_compare.unignored_mismatch_count),
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_cache_ownership(
        const RollbackInputLogOwnershipReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .boolean("context_ready", report.context_ready)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("before_ok", report.before_ok)
         .boolean("after_ok", report.after_ok)
         .boolean("rollback_resim_ok", report.rollback_resim_ok)
         .boolean("same_battle_manager", report.same_battle_manager)
         .boolean("same_input_log", report.same_input_log)
         .boolean("full_hash_match", report.full_hash_match)
         .boolean("cache_hash_match", report.cache_hash_match)
         .boolean("current_input_match", report.current_input_match)
         .boolean("master_clock_match", report.master_clock_match)
         .boolean("drain_cursor_match", report.drain_cursor_match)
         .boolean("warmup_ready", report.warmup_ready)
         .uinteger("min_master_clock", report.min_master_clock)
         .uinteger("warmup_master_clock", report.warmup_master_clock)
         .hex("battle_manager_before", report.before.battle_manager)
         .hex("battle_manager_after", report.after.battle_manager)
         .hex("input_log_before", report.before.input_log)
         .hex("input_log_after", report.after.input_log)
         .uinteger("master_clock_before", report.before.master_clock)
         .uinteger("master_clock_after", report.after.master_clock)
         .uinteger("drain_cursor_before", report.before.drain_cursor)
         .uinteger("drain_cursor_after", report.after.drain_cursor)
         .hex("current_input_p1_before", report.before.current_input[0])
         .hex("current_input_p1_after", report.after.current_input[0])
         .hex("current_input_p2_before", report.before.current_input[1])
         .hex("current_input_p2_after", report.after.current_input[1])
         .hex("full_hash_before", report.before.full_hash)
         .hex("full_hash_after", report.after.full_hash)
         .hex("cache_hash_before", report.before.cache_hash)
         .hex("cache_hash_after", report.after.cache_hash)
         .boolean("resim_corrected_matches_baseline",
                  report.resim.corrected_matches_baseline)
         .boolean("resim_predicted_differs_from_baseline",
                  report.resim.predicted_differs_from_baseline)
         .boolean("resim_baseline_ok", report.resim.baseline_ok)
         .boolean("resim_predicted_ok", report.resim.predicted_ok)
         .boolean("resim_corrected_ok", report.resim.corrected_ok)
         .boolean("resim_restore_start_after_ok",
                  report.resim.restore_start_after_ok)
         .boolean("resim_explicit_match", report.resim.explicit_match)
         .boolean("resim_frame_counter_match",
                  report.resim.frame_counter_match)
         .boolean("resim_frame_counter_delta_ok",
                  report.resim.frame_counter_delta_ok)
         .boolean("resim_all_steps_ok", report.resim.all_steps_ok)
         .boolean("resim_hgcpu_policy_match",
                  report.resim.hgcpu_policy_match)
         .boolean("resim_hgcpu_topology_match",
                  report.resim.corrected_compare.topology_match)
         .boolean("resim_hgcpu_motion_bank_control_match",
                  report.resim.corrected_compare.motion_bank_match)
         .boolean("resim_hgcpu_motion_bank_match",
                  report.resim.corrected_compare.motion_bank_match)
         .boolean("resim_hgcpu_motion_tail_match",
                  report.resim.corrected_compare.motion_tail_match)
         .boolean("resim_hgcpu_timer_node_match",
                  report.resim.corrected_compare.timer_node_match)
         .uinteger("resim_hgcpu_mismatch_count",
                   report.resim.corrected_compare.mismatch_count)
         .uinteger("resim_hgcpu_ignored_mismatch_count",
                   report.resim.corrected_compare.ignored_mismatch_count)
         .uinteger("resim_hgcpu_unignored_mismatch_count",
                   report.resim.corrected_compare.unignored_mismatch_count)
         .uinteger("resim_start_frame", report.resim.start_frame)
         .uinteger("resim_baseline_frame", report.resim.baseline_frame)
         .uinteger("resim_corrected_frame", report.resim.corrected_frame)
         .uinteger("resim_steps_attempted", report.resim.steps_attempted)
         .uinteger("resim_steps_ok", report.resim.steps_ok)
         .hex("resim_baseline_hash", report.resim.baseline_hash)
         .hex("resim_corrected_hash", report.resim.corrected_hash)
         .hex("resim_baseline_explicit_hash",
              report.resim.baseline_explicit_hash)
         .hex("resim_predicted_explicit_hash",
              report.resim.predicted_explicit_hash)
         .hex("resim_corrected_explicit_hash",
              report.resim.corrected_explicit_hash)
         .hex("resim_post_baseline_restore_explicit_hash",
              report.resim.post_baseline_restore_explicit_hash)
         .hex("resim_post_predicted_restore_explicit_hash",
              report.resim.post_predicted_restore_explicit_hash)
         .boolean("resim_post_baseline_restore_explicit_ok",
                  report.resim.post_baseline_restore_explicit_ok)
         .boolean("resim_post_baseline_restore_explicit_match",
                  report.resim.post_baseline_restore_explicit_match)
         .boolean("resim_post_predicted_restore_explicit_ok",
                  report.resim.post_predicted_restore_explicit_ok)
         .boolean("resim_post_predicted_restore_explicit_match",
                  report.resim.post_predicted_restore_explicit_match)
         .boolean("resim_start_lfsr_index_ok",
                  report.resim.start_lfsr_index_ok)
         .boolean("resim_baseline_lfsr_index_ok",
                  report.resim.baseline_lfsr_index_ok)
         .boolean("resim_predicted_lfsr_index_ok",
                  report.resim.predicted_lfsr_index_ok)
         .boolean("resim_corrected_lfsr_index_ok",
                  report.resim.corrected_lfsr_index_ok)
         .boolean("resim_post_baseline_restore_lfsr_index_ok",
                  report.resim.post_baseline_restore_lfsr_index_ok)
         .boolean("resim_post_predicted_restore_lfsr_index_ok",
                  report.resim.post_predicted_restore_lfsr_index_ok)
         .boolean("resim_wind_rng_gate_resolved",
                  report.resim.wind_rng_gate_resolved)
         .boolean("resim_wind_rng_gate_enabled",
                  report.resim.wind_rng_gate_enabled)
         .uinteger("resim_start_lfsr_index",
                   report.resim.start_lfsr_index)
         .uinteger("resim_baseline_lfsr_index",
                   report.resim.baseline_lfsr_index)
         .uinteger("resim_predicted_lfsr_index",
                   report.resim.predicted_lfsr_index)
         .uinteger("resim_corrected_lfsr_index",
                   report.resim.corrected_lfsr_index)
         .uinteger("resim_post_baseline_restore_lfsr_index",
                   report.resim.post_baseline_restore_lfsr_index)
         .uinteger("resim_post_predicted_restore_lfsr_index",
                   report.resim.post_predicted_restore_lfsr_index)
         .string("resim_explicit_mismatch_reason",
                 report.resim.explicit_mismatch_reason)
         .string("resim_explicit_first_mismatch_name",
                 report.resim.explicit_first_mismatch_name)
         .uinteger("resim_explicit_first_mismatch_manifest_index",
                   report.resim.explicit_first_mismatch_manifest_index)
         .hex("resim_explicit_first_mismatch_range_offset",
              report.resim.explicit_first_mismatch_range_offset)
         .hex("resim_explicit_first_mismatch_a",
              report.resim.explicit_first_mismatch_a)
         .hex("resim_explicit_first_mismatch_b",
              report.resim.explicit_first_mismatch_b)
         .hex("resim_explicit_first_mismatch_hash_a",
              report.resim.explicit_first_mismatch_hash_a)
         .hex("resim_explicit_first_mismatch_hash_b",
              report.resim.explicit_first_mismatch_hash_b)
         .hex("resim_first_hgcpu_mismatch_offset",
              report.resim.corrected_compare.first_mismatch_offset)
         .hex("resim_first_hgcpu_mismatch_a",
              report.resim.corrected_compare.first_mismatch_a)
         .hex("resim_first_hgcpu_mismatch_b",
              report.resim.corrected_compare.first_mismatch_b)
         .hex("resim_first_hgcpu_unignored_offset",
              report.resim.corrected_compare.first_unignored_mismatch_offset)
         .hex("resim_first_hgcpu_unignored_a",
              report.resim.corrected_compare.first_unignored_mismatch_a)
         .hex("resim_first_hgcpu_unignored_b",
              report.resim.corrected_compare.first_unignored_mismatch_b)
         .string("resim_first_khit_region",
                 report.resim.corrected_compare.khit_first_region)
         .integer("resim_first_unignored_dynamic_player",
                  report.resim.corrected_compare.first_unignored_dynamic_player)
         .hex("resim_first_unignored_dynamic_local",
              report.resim.corrected_compare.first_unignored_dynamic_local)
         .uinteger("resim_first_khit_player",
                   report.resim.corrected_compare.khit_first_player)
         .integer("resim_first_khit_list",
                  report.resim.corrected_compare.khit_first_list)
         .integer("resim_first_khit_node_index",
                  report.resim.corrected_compare.khit_first_node_index)
         .hex("resim_first_khit_stream_start",
              report.resim.corrected_compare.khit_first_stream_start)
         .hex("resim_first_khit_stream_rel",
              report.resim.corrected_compare.khit_first_stream_rel)
         .uinteger("resim_first_khit_stream_size",
                   report.resim.corrected_compare.khit_first_stream_size)
         .hex("resim_first_khit_node",
              report.resim.corrected_compare.khit_first_node)
         .hex("resim_first_khit_tag",
              report.resim.corrected_compare.khit_first_tag)
         .hex("resim_first_khit_node_source_offset",
              report.resim.corrected_compare.khit_first_node_source_offset)
         .hex("resim_first_khit_node_source_a",
              report.resim.corrected_compare.khit_first_node_source_a)
         .hex("resim_first_khit_node_source_b",
              report.resim.corrected_compare.khit_first_node_source_b)
         .boolean("resim_first_khit_node_source_match",
                  report.resim.corrected_compare.khit_first_node_source_match)
         .hex("resim_first_khit_node_stream_end",
              report.resim.corrected_compare.khit_first_node_stream_end)
         .hex("resim_first_khit_relocation_start",
              report.resim.corrected_compare.khit_first_relocation_start)
         .hex("resim_baseline_motion_bank_hash",
              report.resim.corrected_compare.motion_bank_hash_a)
         .hex("resim_corrected_motion_bank_hash",
              report.resim.corrected_compare.motion_bank_hash_b)
         .hex("resim_baseline_motion_tail_hash",
              report.resim.corrected_compare.motion_tail_hash_a)
         .hex("resim_corrected_motion_tail_hash",
              report.resim.corrected_compare.motion_tail_hash_b)
         .hex("resim_baseline_timer_node_hash",
              report.resim.corrected_compare.timer_node_hash_a)
         .hex("resim_corrected_timer_node_hash",
              report.resim.corrected_compare.timer_node_hash_b)
         .uinteger("resim_hgcpu_timer_indexed_nonzero_count",
                   report.resim.corrected_compare
                       .timer_indexed_nonzero_count_a)
         .uinteger("resim_hgcpu_timer_indexed_captured_count",
                   report.resim.corrected_compare
                       .timer_indexed_captured_count_a)
         .uinteger("resim_hgcpu_timer_indexed_object_captured_count",
                   report.resim.corrected_compare
                       .timer_indexed_object_captured_count_a)
         .hex("resim_hgcpu_timer_slot0_root",
              report.resim.corrected_compare.timer_indexed_slot0_root_a)
         .hex("resim_hgcpu_timer_slot0_vtable",
              report.resim.corrected_compare.timer_indexed_slot0_vtable_a)
         .hex("resim_hgcpu_timer_slot0_writer",
              report.resim.corrected_compare.timer_indexed_slot0_writer_a)
         .boolean("resim_hgcpu_timer_slot0_captured",
                  report.resim.corrected_compare
                      .timer_indexed_slot0_captured_a)
         .hex("resim_baseline_hgcpu_p1_record_bytes",
              report.resim.corrected_compare.p1_record_bytes_a)
         .hex("resim_corrected_hgcpu_p1_record_bytes",
              report.resim.corrected_compare.p1_record_bytes_b)
         .hex("resim_baseline_hgcpu_p2_record_bytes",
              report.resim.corrected_compare.p2_record_bytes_a)
         .hex("resim_corrected_hgcpu_p2_record_bytes",
              report.resim.corrected_compare.p2_record_bytes_b)
         .hex("resim_baseline_hgcpu_p2_base",
              report.resim.corrected_compare.p2_base_a)
         .hex("resim_corrected_hgcpu_p2_base",
              report.resim.corrected_compare.p2_base_b)
         .uinteger("resim_hgcpu_motion_bank_mismatch_count",
                   report.resim.corrected_compare.motion_bank_mismatch_count)
         .string("resim_hgcpu_motion_bank_first_region",
                 report.resim.corrected_compare.motion_bank_first_region)
         .uinteger("resim_hgcpu_motion_bank_first_player",
                   report.resim.corrected_compare.motion_bank_first_player)
         .uinteger("resim_hgcpu_motion_bank_first_bank",
                   report.resim.corrected_compare.motion_bank_first_bank)
         .integer("resim_hgcpu_motion_bank_first_buffer",
                  report.resim.corrected_compare.motion_bank_first_buffer)
         .hex("resim_hgcpu_motion_bank_first_offset",
              report.resim.corrected_compare.motion_bank_first_offset)
         .hex("resim_hgcpu_motion_bank_first_a",
              report.resim.corrected_compare.motion_bank_first_a)
         .hex("resim_hgcpu_motion_bank_first_b",
              report.resim.corrected_compare.motion_bank_first_b)
         .integer("resim_hgcpu_motion_bank_first_slot_a",
                  report.resim.corrected_compare.motion_bank_first_slot_a)
         .integer("resim_hgcpu_motion_bank_first_slot_b",
                  report.resim.corrected_compare.motion_bank_first_slot_b)
         .string("resim_first_ignored_reason",
                 report.resim.corrected_compare.first_ignored_reason)
         .string("failure", report.failure)
         .string("before_failure", report.before.failure)
         .string("after_failure", report.after.failure)
         .string("resim_failure", report.resim.failure);
        ReplayDebugTrace::instance().event(
            "rollback_cache_ownership", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] cache_ownership ok={} context={} window={} "
            "resim={} same_bm={} same_il={} full_hash={} cache_hash={} "
            "current_input={} master={} drain={} warmup={} min_master={} "
            "warmup_master={} bm=0x{:X}->0x{:X} "
            "il=0x{:X}->0x{:X} master_clock={}->{} "
            "drain_cursor={}->{} full=0x{:X}->0x{:X} "
            "cache=0x{:X}->0x{:X} resim_policy={} "
            "resim_motion_bank_control={} resim_motion_tail={} "
            "resim_motion_bank_mismatches={} resim_unignored={} "
            "resim_first_unignored=0x{:X}:0x{:02X}->0x{:02X} "
            "failure={}\n"),
            report.ok ? 1 : 0,
            report.context_ready ? 1 : 0,
            cfg.rollback_window,
            report.rollback_resim_ok ? 1 : 0,
            report.same_battle_manager ? 1 : 0,
            report.same_input_log ? 1 : 0,
            report.full_hash_match ? 1 : 0,
            report.cache_hash_match ? 1 : 0,
            report.current_input_match ? 1 : 0,
            report.master_clock_match ? 1 : 0,
            report.drain_cursor_match ? 1 : 0,
            report.warmup_ready ? 1 : 0,
            report.min_master_clock,
            report.warmup_master_clock,
            static_cast<unsigned long long>(report.before.battle_manager),
            static_cast<unsigned long long>(report.after.battle_manager),
            static_cast<unsigned long long>(report.before.input_log),
            static_cast<unsigned long long>(report.after.input_log),
            report.before.master_clock,
            report.after.master_clock,
            report.before.drain_cursor,
            report.after.drain_cursor,
            static_cast<unsigned long long>(report.before.full_hash),
            static_cast<unsigned long long>(report.after.full_hash),
            static_cast<unsigned long long>(report.before.cache_hash),
            static_cast<unsigned long long>(report.after.cache_hash),
            report.resim.hgcpu_policy_match ? 1 : 0,
            report.resim.corrected_compare.motion_bank_match ? 1 : 0,
            report.resim.corrected_compare.motion_tail_match ? 1 : 0,
            static_cast<unsigned long long>(
                report.resim.corrected_compare.motion_bank_mismatch_count),
            static_cast<unsigned long long>(
                report.resim.corrected_compare.unignored_mismatch_count),
            static_cast<unsigned long long>(
                report.resim.corrected_compare
                    .first_unignored_mismatch_offset),
            report.resim.corrected_compare.first_unignored_mismatch_a,
            report.resim.corrected_compare.first_unignored_mismatch_b,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_online_session_selftest(
        const RollbackOnlineSessionSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("local_packet_ack", report.local_packet_ack)
         .boolean("prediction_created", report.prediction_created)
         .boolean("no_correction_for_matching_prediction",
                  report.no_correction_for_matching_prediction)
         .boolean("correction_for_delayed_mismatch",
                  report.correction_for_delayed_mismatch)
         .boolean("reorder_correction", report.reorder_correction)
         .boolean("duplicate_rejected", report.duplicate_rejected)
         .boolean("conflict_rejected", report.conflict_rejected)
         .boolean("over_window_rejected", report.over_window_rejected)
         .boolean("reorder_preserves_prediction_seed",
                  report.reorder_preserves_prediction_seed)
         .boolean("future_input_not_used_for_earlier_prediction",
                  report.future_input_not_used_for_earlier_prediction)
         .boolean("cache_write_rejected", report.cache_write_rejected)
         .boolean("stock_drain_required", report.stock_drain_required)
         .boolean("drain_bypass_ok", report.drain_bypass_ok)
         .boolean("cache_provenance_ok", report.cache_provenance_ok)
         .boolean("hash_enforced_rejected",
                  report.hash_enforced_rejected)
         .boolean("hash_warn_allows_correction",
                  report.hash_warn_allows_correction)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_online_session_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] online_session ok={} ack={} predict={} "
            "no_correction={} correction={} reorder={} duplicate={} "
            "conflict={} late={} reorder_seed={} no_future_seed={} "
            "cache_write={} stock_drain={} "
            "bypass={} cache_provenance={} "
            "hash_enforced={} hash_warn={} failure={}\n"),
            report.ok ? 1 : 0,
            report.local_packet_ack ? 1 : 0,
            report.prediction_created ? 1 : 0,
            report.no_correction_for_matching_prediction ? 1 : 0,
            report.correction_for_delayed_mismatch ? 1 : 0,
            report.reorder_correction ? 1 : 0,
            report.duplicate_rejected ? 1 : 0,
            report.conflict_rejected ? 1 : 0,
            report.over_window_rejected ? 1 : 0,
            report.reorder_preserves_prediction_seed ? 1 : 0,
            report.future_input_not_used_for_earlier_prediction ? 1 : 0,
            report.cache_write_rejected ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.drain_bypass_ok ? 1 : 0,
            report.cache_provenance_ok ? 1 : 0,
            report.hash_enforced_rejected ? 1 : 0,
            report.hash_warn_allows_correction ? 1 : 0,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_gekko_session_selftest(
        const RollbackGekkoSessionSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("dependency_enabled", report.dependency_enabled)
         .boolean("create_ok", report.create_ok)
         .boolean("start_ok", report.start_ok)
         .boolean("actors_ok", report.actors_ok)
         .boolean("saw_save", report.saw_save)
         .boolean("saw_load", report.saw_load)
         .boolean("saw_advance", report.saw_advance)
         .boolean("saw_rollback_advance", report.saw_rollback_advance)
         .boolean("no_desync", report.no_desync)
         .boolean("final_checksum_expected",
                  report.final_checksum_expected)
         .boolean("destroy_ok", report.destroy_ok)
         .uinteger("frames_submitted", report.frames_submitted)
         .uinteger("save_events", report.save_events)
         .uinteger("load_events", report.load_events)
         .uinteger("advance_events", report.advance_events)
         .uinteger("rollback_advance_events",
                   report.rollback_advance_events)
         .hex("final_checksum", report.final_checksum)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_gekko_session_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] gekko_session ok={} enabled={} create={} "
            "start={} actors={} save={} load={} advance={} "
            "rollback_advance={} no_desync={} checksum_expected={} "
            "destroy={} frames={} "
            "saves={} loads={} advances={} rollback_advances={} "
            "checksum=0x{:X} failure={}\n"),
            report.ok ? 1 : 0,
            report.dependency_enabled ? 1 : 0,
            report.create_ok ? 1 : 0,
            report.start_ok ? 1 : 0,
            report.actors_ok ? 1 : 0,
            report.saw_save ? 1 : 0,
            report.saw_load ? 1 : 0,
            report.saw_advance ? 1 : 0,
            report.saw_rollback_advance ? 1 : 0,
            report.no_desync ? 1 : 0,
            report.final_checksum_expected ? 1 : 0,
            report.destroy_ok ? 1 : 0,
            report.frames_submitted,
            report.save_events,
            report.load_events,
            report.advance_events,
            report.rollback_advance_events,
            report.final_checksum,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_transport_selftest(
        const RollbackLiveTransportQueueSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("bridge_enqueue_ok", report.bridge_enqueue_ok)
         .boolean("bad_bridge_rejected", report.bad_bridge_rejected)
         .boolean("wrong_source_rejected", report.wrong_source_rejected)
         .boolean("wrong_destination_rejected",
                  report.wrong_destination_rejected)
         .boolean("wrong_session_rejected", report.wrong_session_rejected)
         .boolean("network_receive_queued_only",
                  report.network_receive_queued_only)
         .boolean("stock_drain_required", report.stock_drain_required)
         .boolean("game_thread_drain_accepts",
                  report.game_thread_drain_accepts)
         .boolean("correction_required", report.correction_required)
         .boolean("duplicate_drained", report.duplicate_drained)
         .boolean("over_window_rejected", report.over_window_rejected)
         .boolean("drain_bypass_ok", report.drain_bypass_ok)
         .boolean("capacity_guard", report.capacity_guard)
         .uinteger("enqueued_packets", report.enqueued_packets)
         .uinteger("drained_packets", report.drained_packets)
         .uinteger("rejected_packets", report.rejected_packets)
         .uinteger("queue_count", report.queue_count)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_live_transport_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] live_transport ok={} enqueue={} bad={} "
            "wrong_source={} wrong_dest={} wrong_session={} queued_only={} "
            "stock_drain={} drain={} correction={} duplicate={} late={} "
            "bypass={} capacity={} enqueued={} drained={} rejected={} "
            "queued={} failure={}\n"),
            report.ok ? 1 : 0,
            report.bridge_enqueue_ok ? 1 : 0,
            report.bad_bridge_rejected ? 1 : 0,
            report.wrong_source_rejected ? 1 : 0,
            report.wrong_destination_rejected ? 1 : 0,
            report.wrong_session_rejected ? 1 : 0,
            report.network_receive_queued_only ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.game_thread_drain_accepts ? 1 : 0,
            report.correction_required ? 1 : 0,
            report.duplicate_drained ? 1 : 0,
            report.over_window_rejected ? 1 : 0,
            report.drain_bypass_ok ? 1 : 0,
            report.capacity_guard ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.queue_count,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_peer_pipeline_selftest(
        const RollbackLivePeerPipelineSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("bridge_enqueue_ok", report.bridge_enqueue_ok)
         .boolean("network_receive_queued_only",
                  report.network_receive_queued_only)
         .boolean("stock_drain_required",
                  report.stock_drain_required)
         .boolean("metadata_drains_to_session",
                  report.metadata_drains_to_session)
         .boolean("bridge_payload_not_cache_input",
                  report.bridge_payload_not_cache_input)
         .boolean("prediction_cache_write_ok",
                  report.prediction_cache_write_ok)
         .boolean("confirmed_input_replaces_prediction",
                  report.confirmed_input_replaces_prediction)
         .boolean("cache_consume_confirmed",
                  report.cache_consume_confirmed)
         .boolean("duplicate_confirmed_idempotent",
                  report.duplicate_confirmed_idempotent)
         .boolean("prediction_over_confirmed_rejected",
                  report.prediction_over_confirmed_rejected)
         .boolean("wrong_identity_rejected",
                  report.wrong_identity_rejected)
         .boolean("over_window_no_cache_write",
                  report.over_window_no_cache_write)
         .boolean("network_thread_cache_write_rejected",
                  report.network_thread_cache_write_rejected)
         .boolean("drain_bypass_confirmed_input",
                  report.drain_bypass_confirmed_input)
         .uinteger("enqueued_packets", report.enqueued_packets)
         .uinteger("drained_packets", report.drained_packets)
         .uinteger("rejected_packets", report.rejected_packets)
         .uinteger("cache_write_sequence",
                   report.cache_write_sequence)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_live_peer_pipeline_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] live_peer_pipeline ok={} enqueue={} "
            "queued_only={} stock_drain={} metadata={} "
            "payload_not_cache={} predict_cache={} confirm_replace={} "
            "consume_confirmed={} duplicate={} pred_over_confirmed={} "
            "wrong_identity={} late_no_cache={} net_cache_reject={} "
            "bypass={} enqueued={} drained={} rejected={} "
            "cache_writes={} failure={}\n"),
            report.ok ? 1 : 0,
            report.bridge_enqueue_ok ? 1 : 0,
            report.network_receive_queued_only ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.metadata_drains_to_session ? 1 : 0,
            report.bridge_payload_not_cache_input ? 1 : 0,
            report.prediction_cache_write_ok ? 1 : 0,
            report.confirmed_input_replaces_prediction ? 1 : 0,
            report.cache_consume_confirmed ? 1 : 0,
            report.duplicate_confirmed_idempotent ? 1 : 0,
            report.prediction_over_confirmed_rejected ? 1 : 0,
            report.wrong_identity_rejected ? 1 : 0,
            report.over_window_no_cache_write ? 1 : 0,
            report.network_thread_cache_write_rejected ? 1 : 0,
            report.drain_bypass_confirmed_input ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.cache_write_sequence,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_end_to_end_selftest(
        const RollbackEndToEndSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("decoded_payloads", report.decoded_payloads)
         .boolean("bridge_roundtrip", report.bridge_roundtrip)
         .boolean("prediction_written", report.prediction_written)
         .boolean("prediction_diverged", report.prediction_diverged)
         .boolean("adapter_receive_exercised",
                  report.adapter_receive_exercised)
         .boolean("adapter_free_exercised",
                  report.adapter_free_exercised)
         .boolean("metadata_accepted", report.metadata_accepted)
         .boolean("metadata_requires_correction",
                  report.metadata_requires_correction)
         .boolean("metadata_not_gameplay_input",
                  report.metadata_not_gameplay_input)
         .boolean("confirmed_applied", report.confirmed_applied)
         .boolean("confirmed_consumed", report.confirmed_consumed)
         .boolean("initial_baseline_event_order",
                  report.initial_baseline_event_order)
         .boolean("state_converged", report.state_converged)
         .boolean("wrong_identity_rejected",
                  report.wrong_identity_rejected)
         .uinteger("enqueued_packets", report.enqueued_packets)
         .uinteger("drained_packets", report.drained_packets)
         .uinteger("rejected_packets", report.rejected_packets)
         .uinteger("cache_write_sequence", report.cache_write_sequence)
         .hex("predicted_checksum_a", report.predicted_checksum_a)
         .hex("predicted_checksum_b", report.predicted_checksum_b)
         .hex("confirmed_checksum_a", report.confirmed_checksum_a)
         .hex("confirmed_checksum_b", report.confirmed_checksum_b)
         .uinteger("gekko_save_events", report.save_events)
         .uinteger("gekko_load_events", report.load_events)
         .uinteger("gekko_advance_events", report.advance_events)
         .uinteger("gekko_rollback_advance_events",
                   report.rollback_advance_events)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_end_to_end_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] end_to_end ok={} decode={} bridge={} "
            "predict={} predicted_diff={} adapter_receive={} adapter_free={} "
            "metadata={} correction={} metadata_not_gameplay={} "
            "confirm_apply={} confirm_consume={} baseline={} state={} "
            "wrong_identity={} enqueued={} drained={} "
            "rejected={} decoded_inputs={} legacy_pred_a={:#x} "
            "legacy_pred_b={:#x} final_a={:#x} final_b={:#x} "
            "save_events={} load_events={} advance_events={} "
            "rollback_advances={} failure={}\n"),
            report.ok ? 1 : 0,
            report.decoded_payloads ? 1 : 0,
            report.bridge_roundtrip ? 1 : 0,
            report.prediction_written ? 1 : 0,
            report.prediction_diverged ? 1 : 0,
            report.adapter_receive_exercised ? 1 : 0,
            report.adapter_free_exercised ? 1 : 0,
            report.metadata_accepted ? 1 : 0,
            report.metadata_requires_correction ? 1 : 0,
            report.metadata_not_gameplay_input ? 1 : 0,
            report.confirmed_applied ? 1 : 0,
            report.confirmed_consumed ? 1 : 0,
            report.initial_baseline_event_order ? 1 : 0,
            report.state_converged ? 1 : 0,
            report.wrong_identity_rejected ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.cache_write_sequence,
            report.predicted_checksum_a,
            report.predicted_checksum_b,
            report.confirmed_checksum_a,
            report.confirmed_checksum_b,
            report.save_events,
            report.load_events,
            report.advance_events,
            report.rollback_advance_events,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_activation_selftest(
        const RollbackLiveActivationSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("activation_ready", report.activation_ready)
         .boolean("readiness_only_rejected",
                  report.readiness_only_rejected)
         .boolean("stock_surface_rejected",
                  report.stock_surface_rejected)
         .boolean("route_provenance_rejected",
                  report.route_provenance_rejected)
         .boolean("missing_identity_rejected",
                  report.missing_identity_rejected)
         .boolean("direct_readiness_rejected",
                  report.direct_readiness_rejected)
         .boolean("route_identity_rejected",
                  report.route_identity_rejected)
         .boolean("boundary_violation_rejected",
                  report.boundary_violation_rejected)
         .boolean("missing_session_rejected",
                  report.missing_session_rejected)
         .boolean("missing_input_log_rejected",
                  report.missing_input_log_rejected)
         .boolean("self_peer_rejected", report.self_peer_rejected)
         .boolean("zero_session_rejected",
                  report.zero_session_rejected)
         .boolean("operator_not_armed_rejected",
                  report.operator_not_armed_rejected)
         .boolean("missing_receive_rejected",
                  report.missing_receive_rejected)
         .boolean("non_hrg1_rejected", report.non_hrg1_rejected)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_live_activation_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] live_activation ok={} ready={} "
            "readiness_only={} stock={} route_provenance={} "
            "identity={} boundary={} "
            "session={} input_log={} self_peer={} zero_session={} "
            "operator={} receive={} non_hrg1={} direct_ready={} "
            "route_identity={} failure={}\n"),
            report.ok ? 1 : 0,
            report.activation_ready ? 1 : 0,
            report.readiness_only_rejected ? 1 : 0,
            report.stock_surface_rejected ? 1 : 0,
            report.route_provenance_rejected ? 1 : 0,
            report.missing_identity_rejected ? 1 : 0,
            report.boundary_violation_rejected ? 1 : 0,
            report.missing_session_rejected ? 1 : 0,
            report.missing_input_log_rejected ? 1 : 0,
            report.self_peer_rejected ? 1 : 0,
            report.zero_session_rejected ? 1 : 0,
            report.operator_not_armed_rejected ? 1 : 0,
            report.missing_receive_rejected ? 1 : 0,
            report.non_hrg1_rejected ? 1 : 0,
            report.direct_readiness_rejected ? 1 : 0,
            report.route_identity_rejected ? 1 : 0,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_activation_candidate(
        const RollbackLiveActivationReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("activation_ready", report.activation_ready)
         .boolean("explicit_operator_enable",
                  report.explicit_operator_enable)
         .boolean("capture_ready", report.capture_ready)
         .boolean("observe_only", report.observe_only)
         .boolean("stock_observe_ready", report.stock_observe_ready)
         .boolean("boundary_ready", report.boundary_ready)
         .boolean("live_capture_complete",
                  report.live_capture_complete)
         .boolean("no_boundary_violation",
                  report.no_boundary_violation)
         .boolean("stock_send_observed",
                  report.stock_send_observed)
         .boolean("receive_observed", report.receive_observed)
         .boolean("drain_consumer_observed",
                  report.drain_consumer_observed)
         .boolean("live_order_proven", report.live_order_proven)
         .boolean("session_pointer_bound",
                  report.session_pointer_bound)
         .boolean("input_log_bound", report.input_log_bound)
         .boolean("hrg1_payload", report.hrg1_payload)
         .boolean("route_provenance_valid",
                  report.route_provenance_valid)
         .boolean("strict_identity", report.strict_identity)
         .boolean("horse_route_allowed",
                  report.horse_route_allowed)
         .boolean("stock_surface_rejected",
                  report.stock_surface_rejected)
         .boolean("peer_identity_bound",
                  report.peer_identity_bound)
         .boolean("session_id_bound", report.session_id_bound)
         .boolean("route_identity_matches",
                  report.route_identity_matches)
         .uinteger("status",
                   static_cast<uint64_t>(report.status))
         .uinteger("surface_decision",
                   static_cast<uint64_t>(report.surface_decision))
         .uinteger("activation_source_peer",
                   static_cast<uint64_t>(
                       cfg.live_activation_source_peer))
         .uinteger("activation_destination_peer",
                   static_cast<uint64_t>(
                       cfg.live_activation_destination_peer))
         .hex("activation_session_id",
              cfg.live_activation_session_id)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_live_activation_candidate", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] live_activation_candidate ok={} ready={} "
            "request_id={} operator={} capture={} observe_only={} stock={} "
            "boundary={} live={} no_violation={} stock_send={} "
            "receive={} drain_consumer={} live_order={} session_ptr={} "
            "input_log={} hrg1={} provenance={} strict_identity={} "
            "horse_route={} stock_reject={} peer_identity={} "
            "session_id={} route_identity={} source={} dest={} "
            "session=0x{:X} status={} surface={} failure={}\n"),
            report.ok ? 1 : 0,
            report.activation_ready ? 1 : 0,
            RC::to_generic_string(cfg.request_id),
            report.explicit_operator_enable ? 1 : 0,
            report.capture_ready ? 1 : 0,
            report.observe_only ? 1 : 0,
            report.stock_observe_ready ? 1 : 0,
            report.boundary_ready ? 1 : 0,
            report.live_capture_complete ? 1 : 0,
            report.no_boundary_violation ? 1 : 0,
            report.stock_send_observed ? 1 : 0,
            report.receive_observed ? 1 : 0,
            report.drain_consumer_observed ? 1 : 0,
            report.live_order_proven ? 1 : 0,
            report.session_pointer_bound ? 1 : 0,
            report.input_log_bound ? 1 : 0,
            report.hrg1_payload ? 1 : 0,
            report.route_provenance_valid ? 1 : 0,
            report.strict_identity ? 1 : 0,
            report.horse_route_allowed ? 1 : 0,
            report.stock_surface_rejected ? 1 : 0,
            report.peer_identity_bound ? 1 : 0,
            report.session_id_bound ? 1 : 0,
            report.route_identity_matches ? 1 : 0,
            static_cast<uint32_t>(cfg.live_activation_source_peer),
            static_cast<uint32_t>(cfg.live_activation_destination_peer),
            cfg.live_activation_session_id,
            static_cast<uint64_t>(report.status),
            static_cast<uint64_t>(report.surface_decision),
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_activation_executor_selftest(
        const RollbackLiveActivationExecutorSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("activation_required_rejected",
                  report.activation_required_rejected)
         .boolean("readiness_only_rejected",
                  report.readiness_only_rejected)
         .boolean("stock_surface_rejected",
                  report.stock_surface_rejected)
         .boolean("provenance_required_rejected",
                  report.provenance_required_rejected)
         .boolean("route_identity_rejected",
                  report.route_identity_rejected)
         .boolean("activation_ready", report.activation_ready)
         .boolean("live_enqueue_ok", report.live_enqueue_ok)
         .boolean("network_receive_queued_only",
                  report.network_receive_queued_only)
         .boolean("stock_drain_required", report.stock_drain_required)
         .boolean("metadata_drained", report.metadata_drained)
         .boolean("metadata_not_gameplay_input",
                  report.metadata_not_gameplay_input)
         .boolean("prediction_written", report.prediction_written)
         .boolean("decoded_gameplay_applied",
                  report.decoded_gameplay_applied)
         .boolean("confirmed_consumed", report.confirmed_consumed)
          .boolean("network_thread_cache_rejected",
                   report.network_thread_cache_rejected)
          .boolean("wrong_source_rejected", report.wrong_source_rejected)
          .boolean("wrong_destination_rejected",
                   report.wrong_destination_rejected)
          .boolean("wrong_session_rejected", report.wrong_session_rejected)
          .boolean("decoded_route_rejected", report.decoded_route_rejected)
          .uinteger("enqueued_packets", report.enqueued_packets)
         .uinteger("drained_packets", report.drained_packets)
         .uinteger("rejected_packets", report.rejected_packets)
         .uinteger("cache_write_sequence", report.cache_write_sequence)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_live_activation_executor_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] live_activation_executor ok={} "
            "activation_required={} readiness_only={} stock={} "
            "provenance={} route_identity={} ready={} enqueue={} "
            "queued_only={} stock_drain={} metadata={} "
            "metadata_not_gameplay={} predict={} apply={} consume={} "
            "net_cache_reject={} wrong_source={} wrong_dest={} "
            "wrong_session={} decoded_route={} enqueued={} drained={} "
            "rejected={} cache_writes={} "
            "failure={}\n"),
            report.ok ? 1 : 0,
            report.activation_required_rejected ? 1 : 0,
            report.readiness_only_rejected ? 1 : 0,
            report.stock_surface_rejected ? 1 : 0,
            report.provenance_required_rejected ? 1 : 0,
            report.route_identity_rejected ? 1 : 0,
            report.activation_ready ? 1 : 0,
            report.live_enqueue_ok ? 1 : 0,
            report.network_receive_queued_only ? 1 : 0,
            report.stock_drain_required ? 1 : 0,
            report.metadata_drained ? 1 : 0,
            report.metadata_not_gameplay_input ? 1 : 0,
            report.prediction_written ? 1 : 0,
            report.decoded_gameplay_applied ? 1 : 0,
            report.confirmed_consumed ? 1 : 0,
            report.network_thread_cache_rejected ? 1 : 0,
            report.wrong_source_rejected ? 1 : 0,
            report.wrong_destination_rejected ? 1 : 0,
            report.wrong_session_rejected ? 1 : 0,
            report.decoded_route_rejected ? 1 : 0,
            report.enqueued_packets,
            report.drained_packets,
            report.rejected_packets,
            report.cache_write_sequence,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_gekko_adapter_selftest(
        const RollbackGekkoAdapterSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("dependency_enabled", report.dependency_enabled)
         .boolean("create_ok", report.create_ok)
         .boolean("adapter_set", report.adapter_set)
         .boolean("start_ok", report.start_ok)
         .boolean("actors_ok", report.actors_ok)
         .boolean("saw_player_connected", report.saw_player_connected)
         .boolean("saw_session_started", report.saw_session_started)
         .boolean("saw_save", report.saw_save)
         .boolean("saw_load", report.saw_load)
         .boolean("saw_advance", report.saw_advance)
         .boolean("saw_rollback_advance", report.saw_rollback_advance)
         .boolean("no_desync", report.no_desync)
         .boolean("callbacks_sent", report.callbacks_sent)
         .boolean("callbacks_received", report.callbacks_received)
         .boolean("callbacks_freed", report.callbacks_freed)
         .boolean("bidirectional_payloads", report.bidirectional_payloads)
         .boolean("bridge_roundtrip", report.bridge_roundtrip)
         .boolean("bridge_metadata_accepted",
                  report.bridge_metadata_accepted)
         .boolean("bridge_rejections_ok", report.bridge_rejections_ok)
         .boolean("gameplay_inputs_decoded",
                  report.gameplay_inputs_decoded)
         .boolean("gameplay_slots_present",
                  report.gameplay_slots_present)
         .boolean("gameplay_inputs_drive_state",
                  report.gameplay_inputs_drive_state)
         .boolean("final_checksums_match", report.final_checksums_match)
         .boolean("destroy_ok", report.destroy_ok)
         .uinteger("frames_submitted", report.frames_submitted)
         .uinteger("save_events", report.save_events)
         .uinteger("load_events", report.load_events)
         .uinteger("advance_events", report.advance_events)
         .uinteger("rollback_advance_events",
                   report.rollback_advance_events)
         .uinteger("session_events", report.session_events)
         .uinteger("packets_sent", report.packets_sent)
         .uinteger("packets_received", report.packets_received)
         .uinteger("free_calls", report.free_calls)
         .uinteger("bridge_packets_encoded",
                   report.bridge_packets_encoded)
         .uinteger("bridge_packets_decoded",
                   report.bridge_packets_decoded)
         .uinteger("bridge_packets_rejected",
                   report.bridge_packets_rejected)
         .uinteger("gameplay_decoded_events",
                   report.gameplay_decoded_events)
         .uinteger("gameplay_decoded_inputs",
                   report.gameplay_decoded_inputs)
         .hex("final_checksum_a", report.final_checksum_a)
         .hex("final_checksum_b", report.final_checksum_b)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_gekko_adapter_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] gekko_adapter ok={} enabled={} create={} "
            "adapter_set={} start={} actors={} connected={} "
            "session_started={} save={} load={} advance={} "
             "rollback_advance={} no_desync={} sent={} received={} "
             "freed={} bidirectional={} bridge={} bridge_meta={} "
             "bridge_reject={} gameplay_decode={} gameplay_slots={} "
             "gameplay_state={} checksums={} destroy={} "
             "frames={} saves={} loads={} advances={} rollback_advances={} "
             "session_events={} packets_sent={} packets_recv={} frees={} "
             "bridge_encoded={} bridge_decoded={} bridge_bad={} "
             "gameplay_events={} gameplay_inputs={} "
             "checksum_a=0x{:X} checksum_b=0x{:X} failure={}\n"),
            report.ok ? 1 : 0,
            report.dependency_enabled ? 1 : 0,
            report.create_ok ? 1 : 0,
            report.adapter_set ? 1 : 0,
            report.start_ok ? 1 : 0,
            report.actors_ok ? 1 : 0,
            report.saw_player_connected ? 1 : 0,
            report.saw_session_started ? 1 : 0,
            report.saw_save ? 1 : 0,
            report.saw_load ? 1 : 0,
            report.saw_advance ? 1 : 0,
            report.saw_rollback_advance ? 1 : 0,
            report.no_desync ? 1 : 0,
            report.callbacks_sent ? 1 : 0,
            report.callbacks_received ? 1 : 0,
            report.callbacks_freed ? 1 : 0,
            report.bidirectional_payloads ? 1 : 0,
            report.bridge_roundtrip ? 1 : 0,
            report.bridge_metadata_accepted ? 1 : 0,
            report.bridge_rejections_ok ? 1 : 0,
            report.gameplay_inputs_decoded ? 1 : 0,
            report.gameplay_slots_present ? 1 : 0,
            report.gameplay_inputs_drive_state ? 1 : 0,
            report.final_checksums_match ? 1 : 0,
            report.destroy_ok ? 1 : 0,
            report.frames_submitted,
            report.save_events,
            report.load_events,
            report.advance_events,
            report.rollback_advance_events,
            report.session_events,
            report.packets_sent,
            report.packets_received,
            report.free_calls,
            report.bridge_packets_encoded,
            report.bridge_packets_decoded,
            report.bridge_packets_rejected,
            report.gameplay_decoded_events,
            report.gameplay_decoded_inputs,
            report.final_checksum_a,
            report.final_checksum_b,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_gekko_udp_selftest(
        const RollbackGekkoUdpAdapterSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("dependency_enabled", report.dependency_enabled)
         .boolean("wsa_started", report.wsa_started)
         .boolean("sockets_open", report.sockets_open)
          .boolean("bound_loopback", report.bound_loopback)
          .boolean("nonblocking", report.nonblocking)
          .boolean("manual_udp_roundtrip", report.manual_udp_roundtrip)
          .boolean("wrong_endpoint_rejected", report.wrong_endpoint_rejected)
          .boolean("wrong_source_rejected", report.wrong_source_rejected)
         .boolean("wrong_destination_rejected",
                  report.wrong_destination_rejected)
         .boolean("wrong_session_rejected", report.wrong_session_rejected)
         .boolean("create_ok", report.create_ok)
         .boolean("adapter_set", report.adapter_set)
         .boolean("start_ok", report.start_ok)
         .boolean("actors_ok", report.actors_ok)
         .boolean("saw_player_connected", report.saw_player_connected)
         .boolean("saw_session_started", report.saw_session_started)
         .boolean("saw_save", report.saw_save)
         .boolean("saw_load", report.saw_load)
         .boolean("saw_advance", report.saw_advance)
         .boolean("saw_rollback_advance", report.saw_rollback_advance)
         .boolean("no_desync", report.no_desync)
         .boolean("callbacks_sent", report.callbacks_sent)
         .boolean("callbacks_received", report.callbacks_received)
         .boolean("callbacks_freed", report.callbacks_freed)
         .boolean("bidirectional_payloads", report.bidirectional_payloads)
         .boolean("bridge_roundtrip", report.bridge_roundtrip)
         .boolean("bridge_metadata_accepted",
                  report.bridge_metadata_accepted)
         .boolean("gameplay_inputs_decoded",
                  report.gameplay_inputs_decoded)
         .boolean("gameplay_slots_present",
                  report.gameplay_slots_present)
         .boolean("gameplay_inputs_drive_state",
                  report.gameplay_inputs_drive_state)
         .boolean("final_checksums_match", report.final_checksums_match)
         .boolean("destroy_ok", report.destroy_ok)
         .uinteger("frames_submitted", report.frames_submitted)
         .uinteger("save_events", report.save_events)
         .uinteger("load_events", report.load_events)
         .uinteger("advance_events", report.advance_events)
         .uinteger("rollback_advance_events",
                   report.rollback_advance_events)
         .uinteger("session_events", report.session_events)
         .uinteger("packets_sent", report.packets_sent)
         .uinteger("packets_received", report.packets_received)
         .uinteger("free_calls", report.free_calls)
         .uinteger("bridge_packets_encoded",
                   report.bridge_packets_encoded)
         .uinteger("bridge_packets_decoded",
                   report.bridge_packets_decoded)
         .uinteger("bridge_packets_rejected",
                   report.bridge_packets_rejected)
         .uinteger("endpoint_packets_rejected",
                   report.endpoint_packets_rejected)
         .uinteger("gameplay_decoded_events",
                   report.gameplay_decoded_events)
         .uinteger("gameplay_decoded_inputs",
                   report.gameplay_decoded_inputs)
         .uinteger("port_a", report.port_a)
         .uinteger("port_b", report.port_b)
         .hex("final_checksum_a", report.final_checksum_a)
         .hex("final_checksum_b", report.final_checksum_b)
         .integer("wsa_startup_error", report.wsa_startup_error)
         .integer("socket_error", report.socket_error)
         .integer("bind_error", report.bind_error)
         .integer("getsockname_error", report.getsockname_error)
         .integer("ioctlsocket_error", report.ioctlsocket_error)
         .integer("sendto_error", report.sendto_error)
         .integer("recvfrom_error", report.recvfrom_error)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_gekko_udp_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] gekko_udp ok={} enabled={} wsa={} sockets={} "
            "request_id={} "
            "loopback={} nonblocking={} manual={} wrong_endpoint={} "
            "wrong_source={} wrong_dest={} wrong_session={} create={} "
            "adapter_set={} start={} actors={} connected={} "
            "session_started={} save={} load={} advance={} "
            "rollback_advance={} no_desync={} sent={} received={} "
            "freed={} bidirectional={} bridge={} bridge_meta={} "
            "gameplay_decode={} gameplay_slots={} gameplay_state={} "
            "checksums={} destroy={} frames={} saves={} loads={} "
            "advances={} rollback_advances={} session_events={} "
            "packets_sent={} packets_recv={} frees={} bridge_encoded={} "
            "bridge_decoded={} bridge_bad={} endpoint_bad={} "
            "gameplay_events={} gameplay_inputs={} port_a={} port_b={} "
            "checksum_a=0x{:X} checksum_b=0x{:X} "
            "wsa_error={} socket_error={} bind_error={} "
            "getsockname_error={} ioctlsocket_error={} sendto_error={} "
            "recvfrom_error={} failure={}\n"),
            report.ok ? 1 : 0,
            report.dependency_enabled ? 1 : 0,
            report.wsa_started ? 1 : 0,
            report.sockets_open ? 1 : 0,
            RC::to_generic_string(cfg.request_id),
            report.bound_loopback ? 1 : 0,
            report.nonblocking ? 1 : 0,
            report.manual_udp_roundtrip ? 1 : 0,
            report.wrong_endpoint_rejected ? 1 : 0,
            report.wrong_source_rejected ? 1 : 0,
            report.wrong_destination_rejected ? 1 : 0,
            report.wrong_session_rejected ? 1 : 0,
            report.create_ok ? 1 : 0,
            report.adapter_set ? 1 : 0,
            report.start_ok ? 1 : 0,
            report.actors_ok ? 1 : 0,
            report.saw_player_connected ? 1 : 0,
            report.saw_session_started ? 1 : 0,
            report.saw_save ? 1 : 0,
            report.saw_load ? 1 : 0,
            report.saw_advance ? 1 : 0,
            report.saw_rollback_advance ? 1 : 0,
            report.no_desync ? 1 : 0,
            report.callbacks_sent ? 1 : 0,
            report.callbacks_received ? 1 : 0,
            report.callbacks_freed ? 1 : 0,
            report.bidirectional_payloads ? 1 : 0,
            report.bridge_roundtrip ? 1 : 0,
            report.bridge_metadata_accepted ? 1 : 0,
            report.gameplay_inputs_decoded ? 1 : 0,
            report.gameplay_slots_present ? 1 : 0,
            report.gameplay_inputs_drive_state ? 1 : 0,
            report.final_checksums_match ? 1 : 0,
            report.destroy_ok ? 1 : 0,
            report.frames_submitted,
            report.save_events,
            report.load_events,
            report.advance_events,
            report.rollback_advance_events,
            report.session_events,
            report.packets_sent,
            report.packets_received,
            report.free_calls,
            report.bridge_packets_encoded,
            report.bridge_packets_decoded,
            report.bridge_packets_rejected,
            report.endpoint_packets_rejected,
            report.gameplay_decoded_events,
            report.gameplay_decoded_inputs,
            report.port_a,
            report.port_b,
            report.final_checksum_a,
            report.final_checksum_b,
            report.wsa_startup_error,
            report.socket_error,
            report.bind_error,
            report.getsockname_error,
            report.ioctlsocket_error,
            report.sendto_error,
            report.recvfrom_error,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_stock_transport_selftest(
        const RollbackStockTransportSurfaceSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("shared_ptr_layout_ok", report.shared_ptr_layout_ok)
         .boolean("transport_slots_documented",
                  report.transport_slots_documented)
         .boolean("stock_channels_documented",
                  report.stock_channels_documented)
         .boolean("input_slot_rejects_hrg1",
                  report.input_slot_rejects_hrg1)
         .boolean("battle_sync_rejects_hrg1",
                  report.battle_sync_rejects_hrg1)
         .boolean("high_level_kv_rejects_hrg1",
                  report.high_level_kv_rejects_hrg1)
         .boolean("unknown_stock_path_rejected",
                  report.unknown_stock_path_rejected)
         .boolean("adapter_provenance_required",
                  report.adapter_provenance_required)
         .boolean("strict_identity_required",
                  report.strict_identity_required)
         .boolean("strict_identity_values_required",
                  report.strict_identity_values_required)
         .boolean("horse_adapter_allows_hrg1",
                  report.horse_adapter_allows_hrg1)
         .boolean("stock_native_payloads_preserved",
                  report.stock_native_payloads_preserved)
         .boolean("stock_paths_do_not_allow_hrg1",
                  report.stock_paths_do_not_allow_hrg1)
         .boolean("adapter_flag_cannot_override_stock",
                  report.adapter_flag_cannot_override_stock)
         .boolean("bridge_v2_identity_required",
                  report.bridge_v2_identity_required)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_stock_transport_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] stock_transport ok={} shared_ptr={} slots={} "
            "channels={} input_reject={} battle_reject={} kv_reject={} "
            "unknown_reject={} provenance={} identity={} identity_values={} "
            "horse_allow={} "
            "stock_native={} stock_no_hrg1={} flag_override={} "
            "bridge_v2={} failure={}\n"),
            report.ok ? 1 : 0,
            report.shared_ptr_layout_ok ? 1 : 0,
            report.transport_slots_documented ? 1 : 0,
            report.stock_channels_documented ? 1 : 0,
            report.input_slot_rejects_hrg1 ? 1 : 0,
            report.battle_sync_rejects_hrg1 ? 1 : 0,
            report.high_level_kv_rejects_hrg1 ? 1 : 0,
            report.unknown_stock_path_rejected ? 1 : 0,
            report.adapter_provenance_required ? 1 : 0,
            report.strict_identity_required ? 1 : 0,
            report.strict_identity_values_required ? 1 : 0,
            report.horse_adapter_allows_hrg1 ? 1 : 0,
            report.stock_native_payloads_preserved ? 1 : 0,
            report.stock_paths_do_not_allow_hrg1 ? 1 : 0,
            report.adapter_flag_cannot_override_stock ? 1 : 0,
            report.bridge_v2_identity_required ? 1 : 0,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_stock_transport_observe(
        const RollbackStockTransportObserveReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("observe_only", report.observe_only)
         .boolean("hooks_installed", report.hooks_installed)
         .boolean("trace_active", report.trace_active)
         .boolean("acquire_hook_installed",
                  report.acquire_hook_installed)
         .boolean("opcode0_hook_installed",
                  report.opcode0_hook_installed)
         .boolean("opcode1_hook_installed",
                  report.opcode1_hook_installed)
         .boolean("battle_sync_hook_installed",
                  report.battle_sync_hook_installed)
         .boolean("receive_enqueue_hook_installed",
                  report.receive_enqueue_hook_installed)
         .boolean("acquire_observed", report.acquire_observed)
         .boolean("nonnull_session_observed",
                  report.nonnull_session_observed)
         .boolean("stock_input_observed",
                  report.stock_input_observed)
         .boolean("battle_sync_observed",
                  report.battle_sync_observed)
         .boolean("receive_enqueue_observed",
                  report.receive_enqueue_observed)
         .uinteger("acquire_count", report.acquire_count)
         .uinteger("acquire_nonnull_session_count",
                   report.acquire_nonnull_session_count)
         .uinteger("opcode0_count", report.opcode0_count)
         .uinteger("opcode1_count", report.opcode1_count)
         .uinteger("battle_sync_request_stage_count",
                   report.battle_sync_request_stage_count)
         .uinteger("receive_enqueue_count",
                   report.receive_enqueue_count)
         .uinteger("total_observed_calls",
                   report.total_observed_calls)
         .uinteger("last_thread_id", report.last_thread_id)
         .uinteger("last_receive_thread_id",
                   report.last_receive_thread_id)
         .hex("last_out_session_ptr", report.last_out_session_ptr)
         .hex("last_session_ptr", report.last_session_ptr)
         .hex("last_ref_controller_ptr",
              report.last_ref_controller_ptr)
         .hex("last_session_vtable", report.last_session_vtable)
         .hex("last_input_log", report.last_input_log)
         .hex("last_receive_input_log",
              report.last_receive_input_log)
         .hex("last_receive_packet_wrapper",
              report.last_receive_packet_wrapper)
         .uinteger("last_channel", report.last_channel)
         .uinteger("last_msg_type", report.last_msg_type)
         .uinteger("last_receive_flag", report.last_receive_flag)
         .uinteger("last_opcode0_input", report.last_opcode0_input)
         .integer("last_opcode0_frame", report.last_opcode0_frame)
         .uinteger("last_opcode1_slot_mask",
                   report.last_opcode1_slot_mask)
         .integer("last_opcode1_frame", report.last_opcode1_frame)
         .integer("last_opcode1_current_frame",
                  report.last_opcode1_current_frame)
         .integer("last_opcode1_window_frames",
                  report.last_opcode1_window_frames)
         .uinteger("last_opcode1_resend_counter",
                   report.last_opcode1_resend_counter)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_stock_transport_observe", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] stock_observe ok={} observe_only={} hooks={} "
            "active={} acquire_hook={} opcode0_hook={} opcode1_hook={} "
            "battle_hook={} recv_hook={} acquire={} nonnull={} "
            "opcode0={} opcode1={} battle={} recv={} total={} "
            "last_channel={} last_msg={} "
            "last_session=0x{:X} last_vtable=0x{:X} "
            "last_input_log=0x{:X} last_recv_input_log=0x{:X} "
            "last_recv_packet=0x{:X} last_frame={} failure={}\n"),
            report.ok ? 1 : 0,
            report.observe_only ? 1 : 0,
            report.hooks_installed ? 1 : 0,
            report.trace_active ? 1 : 0,
            report.acquire_hook_installed ? 1 : 0,
            report.opcode0_hook_installed ? 1 : 0,
            report.opcode1_hook_installed ? 1 : 0,
            report.battle_sync_hook_installed ? 1 : 0,
            report.receive_enqueue_hook_installed ? 1 : 0,
            report.acquire_count,
            report.acquire_nonnull_session_count,
            report.opcode0_count,
            report.opcode1_count,
            report.battle_sync_request_stage_count,
            report.receive_enqueue_count,
            report.total_observed_calls,
            report.last_channel,
            report.last_msg_type,
            report.last_session_ptr,
            report.last_session_vtable,
            report.last_input_log,
            report.last_receive_input_log,
            report.last_receive_packet_wrapper,
            report.last_opcode1_frame != 0
                ? report.last_opcode1_frame
                : report.last_opcode0_frame,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_online_capture(
        const RollbackLiveOnlineCaptureReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("observe_only", report.observe_only)
         .boolean("capture_ready", report.capture_ready)
         .boolean("live_capture_complete",
                  report.live_capture_complete)
         .boolean("stock_observe_ok", report.stock_observe_ok)
         .boolean("stock_hooks_installed",
                  report.stock_hooks_installed)
         .boolean("stock_trace_active", report.stock_trace_active)
         .boolean("boundary_hooks_installed",
                  report.boundary_hooks_installed)
         .boolean("boundary_trace_active",
                  report.boundary_trace_active)
         .boolean("acquire_observed", report.acquire_observed)
         .boolean("nonnull_session_observed",
                  report.nonnull_session_observed)
         .boolean("stock_input_observed",
                  report.stock_input_observed)
         .boolean("battle_sync_observed",
                  report.battle_sync_observed)
         .boolean("receive_enqueue_observed",
                  report.receive_enqueue_observed)
         .boolean("drain_observed", report.drain_observed)
         .boolean("consumer_observed", report.consumer_observed)
         .boolean("live_order_proven", report.live_order_proven)
         .boolean("boundary_violation", report.boundary_violation)
         .uinteger("acquire_count", report.acquire_count)
         .uinteger("acquire_nonnull_session_count",
                   report.acquire_nonnull_session_count)
         .uinteger("input_send_count", report.input_send_count)
         .uinteger("battle_sync_request_stage_count",
                   report.battle_sync_request_stage_count)
         .uinteger("receive_enqueue_count",
                   report.receive_enqueue_count)
         .uinteger("drain_enter_count", report.drain_enter_count)
         .uinteger("drain_exit_count", report.drain_exit_count)
         .uinteger("consumer_count", report.consumer_count)
         .uinteger("total_observed_calls",
                   report.total_observed_calls)
         .hex("last_session_ptr", report.last_session_ptr)
         .hex("last_input_log", report.last_input_log)
         .hex("last_receive_input_log",
              report.last_receive_input_log)
         .hex("last_receive_packet_wrapper",
              report.last_receive_packet_wrapper)
         .hex("last_battle_manager", report.last_battle_manager)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_live_online_capture", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] live_online_capture ok={} ready={} live={} "
            "request_id={} "
            "observe_only={} stock_ok={} stock_hooks={} stock_active={} "
            "boundary_hooks={} boundary_active={} acquire={} nonnull={} "
            "input={} battle={} recv={} drain_enter={} drain_exit={} "
            "consumer={} live_order={} boundary_violation={} total={} "
            "last_session=0x{:X} last_input_log=0x{:X} "
            "last_recv_packet=0x{:X} last_bm=0x{:X} failure={}\n"),
            report.ok ? 1 : 0,
            report.capture_ready ? 1 : 0,
            report.live_capture_complete ? 1 : 0,
            RC::to_generic_string(cfg.request_id),
            report.observe_only ? 1 : 0,
            report.stock_observe_ok ? 1 : 0,
            report.stock_hooks_installed ? 1 : 0,
            report.stock_trace_active ? 1 : 0,
            report.boundary_hooks_installed ? 1 : 0,
            report.boundary_trace_active ? 1 : 0,
            report.acquire_count,
            report.acquire_nonnull_session_count,
            report.input_send_count,
            report.battle_sync_request_stage_count,
            report.receive_enqueue_count,
            report.drain_enter_count,
            report.drain_exit_count,
            report.consumer_count,
            report.live_order_proven ? 1 : 0,
            report.boundary_violation ? 1 : 0,
            report.total_observed_calls,
            report.last_session_ptr,
            report.last_input_log,
            report.last_receive_packet_wrapper,
            report.last_battle_manager,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_boundary(
        const RollbackLiveBoundaryReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("hooks_installed", report.hooks_installed)
         .boolean("trace_active", report.trace_active)
         .boolean("stock_drain_inert", report.stock_drain_inert)
         .boolean("live_order_proven", report.live_order_proven)
         .boolean("offline_boundary_observed",
                  report.offline_boundary_observed)
         .boolean("consumer_after_completed_drain",
                  report.consumer_after_completed_drain)
         .boolean("consumer_during_drain",
                  report.consumer_during_drain)
         .boolean("unbalanced_drain", report.unbalanced_drain)
         .uinteger("sequence", report.sequence)
         .uinteger("drain_enter_count", report.drain_enter_count)
         .uinteger("drain_exit_count", report.drain_exit_count)
         .uinteger("consumer_count", report.consumer_count)
         .uinteger("first_drain_enter_sequence",
                   report.first_drain_enter_sequence)
         .uinteger("last_drain_exit_sequence",
                   report.last_drain_exit_sequence)
         .uinteger("first_consumer_sequence",
                   report.first_consumer_sequence)
         .uinteger("first_consumer_after_drain_sequence",
                   report.first_consumer_after_drain_sequence)
         .uinteger("last_drain_thread_id", report.last_drain_thread_id)
         .uinteger("last_consumer_thread_id",
                   report.last_consumer_thread_id)
         .hex("last_input_log", report.last_input_log)
         .hex("last_battle_manager", report.last_battle_manager)
         .uinteger("last_player_index", report.last_player_index)
         .integer("last_cache_frame", report.last_cache_frame)
         .uinteger("last_master_clock", report.last_master_clock)
         .integer("last_frames_back", report.last_frames_back)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_live_boundary", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] live_boundary ok={} hooks={} active={} "
            "drain_inert={} live_order={} offline_boundary={} "
            "consumer_after_drain={} consumer_during_drain={} "
            "unbalanced_drain={} drain_enter={} drain_exit={} "
            "consumer={} seq={} first_consumer={} after_drain_consumer={} "
            "drain_thread={} consumer_thread={} bm=0x{:X} il=0x{:X} "
            "player={} cache_frame={} master={} frames_back={} "
            "failure={}\n"),
            report.ok ? 1 : 0,
            report.hooks_installed ? 1 : 0,
            report.trace_active ? 1 : 0,
            report.stock_drain_inert ? 1 : 0,
            report.live_order_proven ? 1 : 0,
            report.offline_boundary_observed ? 1 : 0,
            report.consumer_after_completed_drain ? 1 : 0,
            report.consumer_during_drain ? 1 : 0,
            report.unbalanced_drain ? 1 : 0,
            static_cast<unsigned long long>(report.drain_enter_count),
            static_cast<unsigned long long>(report.drain_exit_count),
            static_cast<unsigned long long>(report.consumer_count),
            static_cast<unsigned long long>(report.sequence),
            static_cast<unsigned long long>(
                report.first_consumer_sequence),
            static_cast<unsigned long long>(
                report.first_consumer_after_drain_sequence),
            report.last_drain_thread_id,
            report.last_consumer_thread_id,
            static_cast<unsigned long long>(report.last_battle_manager),
            static_cast<unsigned long long>(report.last_input_log),
            report.last_player_index,
            report.last_cache_frame,
            report.last_master_clock,
            report.last_frames_back,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_cache_injection(
        const RollbackCacheInjectionReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("hooks_installed", report.hooks_installed)
         .boolean("probe_active", report.probe_active)
         .boolean("attempted", report.attempted)
         .boolean("context_ready", report.context_ready)
         .boolean("source_cell_valid", report.source_cell_valid)
         .boolean("wrote_cache", report.wrote_cache)
         .boolean("consumer_observed_cache",
                  report.consumer_observed_cache)
         .boolean("restored_cache", report.restored_cache)
         .boolean("restored_current_input",
                  report.restored_current_input)
         .boolean("restored_output_pair", report.restored_output_pair)
         .boolean("idempotent_write", report.idempotent_write)
         .boolean("non_idempotent_write",
                  report.non_idempotent_write)
         .boolean("injected_differs_from_original",
                  report.injected_differs_from_original)
         .boolean("output_pair_observed_prediction",
                  report.output_pair_observed_prediction)
         .boolean("network_event_mask_inferred",
                  report.network_event_mask_inferred)
         .uinteger("invalid_context_count",
                   report.invalid_context_count)
         .uinteger("player_index", report.dwPlayerIndex)
         .uinteger("master_clock", report.dwMasterClock)
         .uinteger("forbidden_input_mask",
                   report.dwForbiddenInputMask)
         .integer("frames_back", report.nFramesBack)
         .integer("frame_index", report.nFrameIndex)
         .integer("frame_id", report.nFrameID)
         .uinteger("original_input", report.dwOriginalInput)
         .uinteger("injected_input", report.dwInjectedInput)
         .uinteger("observed_current_input",
                   report.dwObservedCurrentInput)
         .uinteger("restored_current_input_value",
                   report.dwRestoredCurrentInput)
         .uinteger("observed_output_input",
                   report.observed_output_pair.dwInputWord)
         .uinteger("observed_output_flags",
                   report.observed_output_pair.dwFlags)
         .uinteger("expected_injected_output_input",
                   report.expected_injected_output_pair.dwInputWord)
         .uinteger("expected_injected_output_flags",
                   report.expected_injected_output_pair.dwFlags)
         .uinteger("expected_restored_output_input",
                   report.expected_restored_output_pair.dwInputWord)
         .uinteger("expected_restored_output_flags",
                   report.expected_restored_output_pair.dwFlags)
         .uinteger("restored_output_input",
                   report.restored_output_pair_value.dwInputWord)
         .uinteger("restored_output_flags",
                   report.restored_output_pair_value.dwFlags)
         .hex("battle_manager", report.pBattleManager)
         .hex("input_log", report.pInputLog)
         .hex("cache_entry", report.pCacheEntry)
         .hex("current_input_slot", report.pCurrentInputSlot)
         .hex("input_pair_slot", report.pInputPairSlot)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            report.non_idempotent_write
                ? "rollback_cache_prediction"
                : "rollback_cache_injection",
            f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] {} ok={} hooks={} active={} "
            "attempted={} context={} source={} wrote={} observed={} "
            "restored={} restored_cur={} restored_pair={} "
            "idempotent={} non_idempotent={} differs={} pair_observed={} "
            "invalid_contexts={} "
            "player={} frame={} frame_id={} master={} frames_back={} "
            "original=0x{:X} injected=0x{:X} observed_input=0x{:X} "
            "restored_input=0x{:X} observed_pair=0x{:X}/0x{:X} "
            "expected_pair=0x{:X}/0x{:X} restored_pair=0x{:X}/0x{:X} "
            "bm=0x{:X} il=0x{:X} entry=0x{:X} current=0x{:X} "
            "pair=0x{:X} "
            "failure={}\n"),
            RC::to_generic_string(std::string(
                report.non_idempotent_write
                    ? "cache_prediction"
                    : "cache_injection")),
            report.ok ? 1 : 0,
            report.hooks_installed ? 1 : 0,
            report.probe_active ? 1 : 0,
            report.attempted ? 1 : 0,
            report.context_ready ? 1 : 0,
            report.source_cell_valid ? 1 : 0,
            report.wrote_cache ? 1 : 0,
            report.consumer_observed_cache ? 1 : 0,
            report.restored_cache ? 1 : 0,
            report.restored_current_input ? 1 : 0,
            report.restored_output_pair ? 1 : 0,
            report.idempotent_write ? 1 : 0,
            report.non_idempotent_write ? 1 : 0,
            report.injected_differs_from_original ? 1 : 0,
            report.output_pair_observed_prediction ? 1 : 0,
            report.invalid_context_count,
            report.dwPlayerIndex,
            report.nFrameIndex,
            report.nFrameID,
            report.dwMasterClock,
            report.nFramesBack,
            report.dwOriginalInput,
            report.dwInjectedInput,
            report.dwObservedCurrentInput,
            report.dwRestoredCurrentInput,
            report.observed_output_pair.dwInputWord,
            report.observed_output_pair.dwFlags,
            report.expected_injected_output_pair.dwInputWord,
            report.expected_injected_output_pair.dwFlags,
            report.restored_output_pair_value.dwInputWord,
            report.restored_output_pair_value.dwFlags,
            static_cast<unsigned long long>(report.pBattleManager),
            static_cast<unsigned long long>(report.pInputLog),
            static_cast<unsigned long long>(report.pCacheEntry),
            static_cast<unsigned long long>(report.pCurrentInputSlot),
            static_cast<unsigned long long>(report.pInputPairSlot),
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_gekko_gameplay_input_selftest(
        const RollbackGekkoGameplayInputBridgeSelfTestReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .uinteger("rollback_window", cfg.rollback_window)
         .hex("seed", cfg.seed)
         .boolean("dependency_enabled", report.dependency_enabled)
         .boolean("raw_decode_ok", report.raw_decode_ok)
         .boolean("raw_decode_player0", report.raw_decode_player0)
         .boolean("raw_decode_player1", report.raw_decode_player1)
         .boolean("null_inputs_rejected", report.null_inputs_rejected)
         .boolean("bad_frame_rejected", report.bad_frame_rejected)
         .boolean("bad_size_rejected", report.bad_size_rejected)
         .boolean("bad_player_count_rejected",
                  report.bad_player_count_rejected)
         .boolean("bad_slot_rejected", report.bad_slot_rejected)
         .boolean("pipeline_apply_ok", report.pipeline_apply_ok)
         .boolean("payload_hash_separate", report.payload_hash_separate)
         .boolean("create_ok", report.create_ok)
         .boolean("start_ok", report.start_ok)
         .boolean("actors_ok", report.actors_ok)
         .boolean("actual_gekko_advance_decode",
                  report.actual_gekko_advance_decode)
         .boolean("actual_gekko_rollback_decode",
                  report.actual_gekko_rollback_decode)
         .boolean("no_desync", report.no_desync)
         .boolean("destroy_ok", report.destroy_ok)
         .boolean("final_checksum_expected",
                  report.final_checksum_expected)
         .uinteger("decoded_events", report.decoded_events)
         .uinteger("decoded_inputs", report.decoded_inputs)
         .uinteger("frames_submitted", report.frames_submitted)
         .uinteger("advance_events", report.advance_events)
         .uinteger("rollback_advance_events",
                   report.rollback_advance_events)
         .hex("final_checksum", report.final_checksum)
         .string("failure", report.failure);
        ReplayDebugTrace::instance().event(
            "rollback_gekko_gameplay_input_selftest", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] gekko_gameplay_input ok={} enabled={} raw={} "
            "raw_p0={} raw_p1={} null={} bad_frame={} bad_size={} "
            "bad_players={} "
            "bad_slot={} pipeline={} payload_separate={} create={} "
            "start={} actors={} advance_decode={} rollback_decode={} "
            "no_desync={} destroy={} decoded_events={} decoded_inputs={} "
            "frames={} advances={} rollback_advances={} checksum=0x{:X} "
            "checksum_expected={} failure={}\n"),
            report.ok ? 1 : 0,
            report.dependency_enabled ? 1 : 0,
            report.raw_decode_ok ? 1 : 0,
            report.raw_decode_player0 ? 1 : 0,
            report.raw_decode_player1 ? 1 : 0,
            report.null_inputs_rejected ? 1 : 0,
            report.bad_frame_rejected ? 1 : 0,
            report.bad_size_rejected ? 1 : 0,
            report.bad_player_count_rejected ? 1 : 0,
            report.bad_slot_rejected ? 1 : 0,
            report.pipeline_apply_ok ? 1 : 0,
            report.payload_hash_separate ? 1 : 0,
            report.create_ok ? 1 : 0,
            report.start_ok ? 1 : 0,
            report.actors_ok ? 1 : 0,
            report.actual_gekko_advance_decode ? 1 : 0,
            report.actual_gekko_rollback_decode ? 1 : 0,
            report.no_desync ? 1 : 0,
            report.destroy_ok ? 1 : 0,
            report.decoded_events,
            report.decoded_inputs,
            report.frames_submitted,
            report.advance_events,
            report.rollback_advance_events,
            report.final_checksum,
            report.final_checksum_expected ? 1 : 0,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_sidecar_bind(
        const RollbackSidecarReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        const bool bind_ok =
            report.enabled
            && report.wsa_started
            && report.socket_open
            && report.bound_loopback
            && report.nonblocking
            && report.udp_connreset_disabled
            && !report.reserved_steam_port_rejected;
        ReplayTraceFields f;
        f.boolean("ok", bind_ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .string("client_role", cfg.client_role)
         .uinteger("local_peer_id", report.local_peer)
         .uinteger("remote_peer_id", report.remote_peer)
         .hex("session_id", report.session_id)
         .uinteger("sidecar_local_port", report.local_port)
         .uinteger("sidecar_remote_port", report.remote_port)
         .string("sidecar_remote_addr", cfg.sidecar_remote_addr)
         .hex("activation_token_hash", report.activation_token_hash)
         .boolean("enabled", report.enabled)
         .boolean("wsa_started", report.wsa_started)
         .boolean("socket_open", report.socket_open)
          .boolean("bound_loopback", report.bound_loopback)
          .boolean("nonblocking", report.nonblocking)
          .boolean("udp_connreset_disabled",
                   report.udp_connreset_disabled)
          .boolean("reserved_steam_port_rejected",
                   report.reserved_steam_port_rejected)
         .integer("wsa_startup_error", report.wsa_startup_error)
         .integer("socket_error", report.socket_error)
          .integer("bind_error", report.bind_error)
          .integer("ioctlsocket_error", report.ioctlsocket_error)
          .integer("udp_connreset_error", report.udp_connreset_error)
          .string("failure", report.failure);
        ReplayDebugTrace::instance().event("rollback_sidecar_bind", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] sidecar_bind ok={} role={} local_port={} "
            "remote_port={} wsa={} socket={} bind={} nb={} udp_reset={} "
            "failure={}\n"),
            bind_ok ? 1 : 0,
            RC::to_generic_string(cfg.client_role),
            report.local_port,
            report.remote_port,
            report.wsa_started ? 1 : 0,
            report.socket_open ? 1 : 0,
            report.bound_loopback ? 1 : 0,
            report.nonblocking ? 1 : 0,
            report.udp_connreset_disabled ? 1 : 0,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_sidecar_handshake(
        const RollbackSidecarReport& report,
        const RollbackLabConfig& cfg) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", report.ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .string("client_role", cfg.client_role)
         .uinteger("local_peer_id", report.local_peer)
         .uinteger("remote_peer_id", report.remote_peer)
         .hex("session_id", report.session_id)
         .uinteger("sidecar_local_port", report.local_port)
         .uinteger("sidecar_remote_port", report.remote_port)
         .hex("activation_token_hash", report.activation_token_hash)
         .boolean("sent_hello", report.sent_hello)
         .boolean("received_hello", report.received_hello)
          .boolean("validated_peer", report.validated_peer)
          .boolean("udp_connreset_disabled",
                   report.udp_connreset_disabled)
          .boolean("direct_input_enabled", report.direct_input_enabled)
         .boolean("sent_direct_input", report.sent_direct_input)
         .boolean("received_direct_input", report.received_direct_input)
         .boolean("validated_direct_input",
                  report.validated_direct_input)
         .boolean("stock_offer_enabled", report.stock_offer_enabled)
         .boolean("sent_stock_offer", report.sent_stock_offer)
         .boolean("received_stock_offer", report.received_stock_offer)
         .boolean("validated_stock_offer", report.validated_stock_offer)
         .boolean("remote_stock_fallback_ready",
                  report.remote_stock_fallback_ready)
         .boolean("wrong_stock_offer_rejected",
                  report.wrong_stock_offer_rejected)
         .hex("local_steam_id", report.local_steam_id)
         .hex("remote_steam_id", report.remote_steam_id)
         .hex("stock_offer_lobby_id",
              report.remote_stock_offer.lobby_id)
         .hex("stock_offer_host_steam_id",
              report.remote_stock_offer.host_steam_id)
         .hex("stock_offer_invitee_steam_id",
              report.remote_stock_offer.invitee_steam_id)
         .hex("stock_offer_generation",
              report.remote_stock_offer.request_generation)
         .hex("stock_offer_build_id",
              report.remote_stock_offer.build_id)
         .hex("stock_offer_schema_id",
              report.remote_stock_offer.schema_id)
         .hex("stock_offer_authentication_tag",
              report.remote_stock_offer.authentication_tag)
         .uinteger("stock_offer_packets_sent",
                   report.stock_offer_packets_sent)
         .uinteger("stock_offer_packets_received",
                   report.stock_offer_packets_received)
         .uinteger("stock_offer_packets_rejected",
                   report.stock_offer_packets_rejected)
         .boolean("direct_payload_hash_valid",
                  report.direct_payload_hash_valid)
         .boolean("wrong_endpoint_rejected",
                  report.wrong_endpoint_rejected)
         .boolean("wrong_route_rejected", report.wrong_route_rejected)
         .boolean("wrong_token_rejected", report.wrong_token_rejected)
         .boolean("wrong_packet_type_rejected",
                  report.wrong_packet_type_rejected)
         .boolean("wrong_direct_sequence_rejected",
                  report.wrong_direct_sequence_rejected)
         .boolean("wrong_direct_payload_rejected",
                  report.wrong_direct_payload_rejected)
         .uinteger("direct_packets_sent", report.direct_packets_sent)
         .uinteger("direct_packets_received",
                   report.direct_packets_received)
         .uinteger("direct_packets_rejected",
                   report.direct_packets_rejected)
         .uinteger("local_replay_player", report.local_replay_player)
         .uinteger("remote_replay_player", report.remote_replay_player)
         .uinteger("local_first_frame", report.local_first_frame)
         .uinteger("local_frame_count", report.local_frame_count)
         .uinteger("remote_first_frame", report.remote_first_frame)
         .uinteger("remote_frame_count", report.remote_frame_count)
         .hex("local_input_hash", report.local_input_hash)
         .hex("expected_remote_input_hash",
              report.expected_remote_input_hash)
         .hex("remote_input_hash", report.remote_input_hash)
         .hex("local_direct_payload_hash",
              report.local_direct_payload_hash)
         .hex("remote_direct_payload_hash",
              report.remote_direct_payload_hash)
         .uinteger("packets_sent", report.packets_sent)
         .uinteger("packets_received", report.packets_received)
         .uinteger("packets_rejected", report.packets_rejected)
          .integer("sendto_error", report.sendto_error)
          .integer("recvfrom_error", report.recvfrom_error)
          .integer("udp_connreset_error", report.udp_connreset_error)
          .string("failure", report.failure);
        ReplayDebugTrace::instance().event("rollback_sidecar_handshake", f);

        RC::Output::send<RC::LogLevel::Default>(STR(
            "[RollbackLab] sidecar_handshake ok={} role={} sent={} recv={} "
            "validated={} rejected={} failure={}\n"),
            report.ok ? 1 : 0,
            RC::to_generic_string(cfg.client_role),
            report.packets_sent,
            report.packets_received,
            report.validated_peer ? 1 : 0,
            report.packets_rejected,
            RC::to_generic_string(std::string(
                report.failure ? report.failure : "?")));
    }

    inline void RollbackDiag::emit_live_cache_write(
        const RollbackSidecarReport& sidecar,
        const RollbackLiveOnlineCaptureReport& capture,
        const RollbackLiveActivationReport& activation,
        const RollbackResimWindowReport& resim,
        const RollbackLabConfig& cfg) noexcept
    {
        const bool stock_drain_before_write = capture.drain_exit_count > 0;
        const bool consumer_after_write = capture.consumer_count > 0;
        const bool ok =
            sidecar.ok
            && activation.activation_ready
            && capture.live_capture_complete
            && resim.context_ready
            && resim.predicted_ok
            && resim.predicted_differs_from_baseline
            && stock_drain_before_write
            && consumer_after_write;
        ReplayTraceFields f;
        f.boolean("ok", ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .string("client_role", cfg.client_role)
         .string("proof_source", "activation-gated-resim-probe")
         .boolean("game_thread", true)
         .boolean("network_thread", false)
         .boolean("sidecar_ready", sidecar.ok)
         .boolean("activation_ready", activation.activation_ready)
         .boolean("live_capture_complete", capture.live_capture_complete)
         .boolean("stock_drain_before_write", stock_drain_before_write)
         .boolean("consumer_after_write", consumer_after_write)
         .boolean("prediction_written", resim.predicted_ok)
         .boolean("prediction_diverged",
                  resim.predicted_differs_from_baseline)
         .boolean("local_input_delay_measured", false)
         .string("local_input_delay_policy", "stock-unmodified")
         .uinteger("write_frame", resim.fault_frame_index)
         .uinteger("rollback_window", resim.window)
         .hex("predicted_hash", resim.predicted_hash)
         .hex("baseline_hash", resim.baseline_hash)
         .hex("corrected_hash", resim.corrected_hash)
         .string("failure", ok ? "ok" : resim.failure);
        ReplayDebugTrace::instance().event("rollback_live_cache_write", f);
    }

    inline void RollbackDiag::emit_live_correction(
        const RollbackSidecarReport& sidecar,
        const RollbackLiveOnlineCaptureReport& capture,
        const RollbackLiveActivationReport& activation,
        const RollbackResimWindowReport& resim,
        const RollbackLabConfig& cfg) noexcept
    {
        const bool nonzero_depth = resim.window > 0;
        const bool ok =
            sidecar.ok
            && activation.activation_ready
            && capture.live_capture_complete
            && resim.ok
            && resim.predicted_differs_from_baseline
            && resim.corrected_matches_baseline
            && nonzero_depth;
        ReplayTraceFields f;
        f.boolean("ok", ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .string("client_role", cfg.client_role)
         .string("proof_source", "activation-gated-resim-probe")
         .boolean("sidecar_ready", sidecar.ok)
         .boolean("activation_ready", activation.activation_ready)
         .boolean("live_capture_complete", capture.live_capture_complete)
         .boolean("confirmed_input_applied", resim.corrected_ok)
         .boolean("correction_scheduled",
                  resim.predicted_differs_from_baseline)
         .boolean("nonzero_correction_depth", nonzero_depth)
         .boolean("snapshot_restore", resim.restore_start_after_ok)
         .boolean("hidden_resim", resim.steps_ok > 0)
         .boolean("corrected_matches_baseline",
                  resim.corrected_matches_baseline)
         .boolean("predicted_differs_from_baseline",
                  resim.predicted_differs_from_baseline)
         .uinteger("correction_depth", resim.window)
         .uinteger("steps_ok", resim.steps_ok)
         .uinteger("steps_attempted", resim.steps_attempted)
         .hex("baseline_hash", resim.baseline_hash)
         .hex("predicted_hash", resim.predicted_hash)
         .hex("corrected_hash", resim.corrected_hash)
         .string("failure", ok ? "ok" : resim.failure);
        ReplayDebugTrace::instance().event("rollback_live_correction", f);
    }

    inline void RollbackDiag::emit_live_convergence(
        const RollbackSidecarReport& sidecar,
        const RollbackLiveOnlineCaptureReport& capture,
        const RollbackLiveActivationReport& activation,
        const RollbackResimWindowReport& resim,
        const RollbackLabConfig& cfg) noexcept
    {
        const bool converged =
            resim.corrected_matches_baseline
            && resim.corrected_hash != 0
            && resim.corrected_hash == resim.baseline_hash;
        const bool ok =
            sidecar.ok
            && activation.activation_ready
            && capture.live_capture_complete
            && resim.ok
            && converged;
        ReplayTraceFields f;
        f.boolean("ok", ok)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .string("client_role", cfg.client_role)
         .string("proof_source", "activation-gated-resim-probe")
         .boolean("peer_hash_exchange", false)
         .boolean("converged", converged)
         .boolean("sidecar_ready", sidecar.ok)
         .boolean("activation_ready", activation.activation_ready)
         .boolean("live_capture_complete", capture.live_capture_complete)
         .hex("local_corrected_hash", resim.corrected_hash)
         .hex("peer_corrected_hash", 0)
         .hex("baseline_hash", resim.baseline_hash)
         .hex("predicted_hash", resim.predicted_hash)
         .string("peer_hash_source", "runner-cross-client-required")
         .string("failure", ok ? "ok" : resim.failure);
        ReplayDebugTrace::instance().event("rollback_live_convergence", f);
    }

    inline void RollbackDiag::emit_live_disarm(
        const RollbackSidecarReport& sidecar,
        const RollbackLiveOnlineCaptureReport& capture,
        const RollbackLiveActivationReport& activation,
        const RollbackLabConfig& cfg,
        const char* reason) noexcept
    {
        ReplayTraceFields f;
        f.boolean("ok", false)
         .boolean("disarmed", true)
         .string("case", rollback_case_name(cfg.test_case))
         .string("request_id", cfg.request_id)
         .string("client_role", cfg.client_role)
         .string("reason", reason ? reason : "disarmed")
         .boolean("sidecar_ready", sidecar.ok)
         .boolean("sidecar_bound", sidecar.bound_loopback)
         .boolean("sidecar_validated_peer", sidecar.validated_peer)
         .boolean("activation_ready", activation.activation_ready)
         .boolean("operator_armed",
                  activation.explicit_operator_enable)
         .boolean("capture_ready", capture.capture_ready)
         .boolean("live_capture_complete", capture.live_capture_complete)
         .boolean("boundary_violation", capture.boundary_violation)
         .uinteger("sidecar_local_port", sidecar.local_port)
         .uinteger("sidecar_remote_port", sidecar.remote_port)
         .string("sidecar_failure", sidecar.failure)
         .string("activation_failure", activation.failure)
         .string("capture_failure", capture.failure);
        ReplayDebugTrace::instance().event("rollback_live_disarm", f);
    }
}
