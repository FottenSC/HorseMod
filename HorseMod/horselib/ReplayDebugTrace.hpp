// ============================================================================
// Horse::ReplayDebugTrace
//
// Best-effort JSONL trace writer for replay timeline generation and SC6 exact
// seek diagnostics.  This is observability only: failures to open/write the
// trace file never alter replay behavior.
// ============================================================================

#pragma once

#include "NativeBinding.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Horse
{
    struct NativeCallFault
    {
        bool faulted {false};
        uint32_t exception_code {0};
        uintptr_t exception_address {0};
    };

    static inline int CaptureNativeCallFault(
        unsigned code,
        EXCEPTION_POINTERS* ep,
        NativeCallFault* out) noexcept
    {
        if (out)
        {
            out->faulted = true;
            out->exception_code = static_cast<uint32_t>(code);
            out->exception_address =
                ep && ep->ExceptionRecord
                    ? reinterpret_cast<uintptr_t>(
                          ep->ExceptionRecord->ExceptionAddress)
                    : 0;
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }

    struct ReplayFunctionMapEntry
    {
        uintptr_t start_rva {0};
        uintptr_t end_rva {0};
        std::string name;
    };

    class ReplayFunctionMap
    {
    public:
        bool load_tsv(const std::wstring& path) noexcept
        {
            std::string text;
            if (!read_file(path, text))
                return false;

            std::vector<ReplayFunctionMapEntry> parsed;
            size_t pos = 0;
            while (pos < text.size())
            {
                const size_t eol = text.find_first_of("\r\n", pos);
                const size_t end =
                    eol == std::string::npos ? text.size() : eol;
                std::string line = text.substr(pos, end - pos);
                pos = end;
                while (pos < text.size()
                       && (text[pos] == '\r' || text[pos] == '\n'))
                    ++pos;

                if (line.empty()) continue;
                if (line.starts_with("image\t")
                    || line.starts_with("image_base\t")
                    || line.starts_with("start_rva\t"))
                    continue;

                const size_t t0 = line.find('\t');
                if (t0 == std::string::npos) continue;
                const size_t t1 = line.find('\t', t0 + 1);
                if (t1 == std::string::npos) continue;

                ReplayFunctionMapEntry e{};
                if (!parse_hex(line.substr(0, t0), e.start_rva)
                    || !parse_hex(line.substr(t0 + 1, t1 - t0 - 1),
                                  e.end_rva))
                    continue;
                e.name = line.substr(t1 + 1);
                if (e.start_rva == 0 || e.name.empty())
                    continue;
                if (e.end_rva <= e.start_rva)
                    e.end_rva = e.start_rva + 1;
                parsed.push_back(std::move(e));
            }

            if (parsed.empty())
                return false;

            std::sort(parsed.begin(), parsed.end(),
                      [](const ReplayFunctionMapEntry& a,
                         const ReplayFunctionMapEntry& b)
                      {
                          return a.start_rva < b.start_rva;
                      });
            m_entries = std::move(parsed);
            m_loaded_path = path;
            return true;
        }

        const ReplayFunctionMapEntry* lookup_rva(uintptr_t rva) const noexcept
        {
            if (m_entries.empty()) return nullptr;
            size_t lo = 0;
            size_t hi = m_entries.size();
            while (lo < hi)
            {
                const size_t mid = lo + (hi - lo) / 2;
                if (m_entries[mid].start_rva <= rva)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo == 0) return nullptr;
            const ReplayFunctionMapEntry& e = m_entries[lo - 1];
            return rva < e.end_rva ? &e : nullptr;
        }

        std::string format_absolute_rip(uintptr_t image_base,
                                        uintptr_t rip) const
        {
            if (!image_base || !rip || rip < image_base)
                return {};
            const uintptr_t rva = rip - image_base;
            char buf[64]{};
            if (const ReplayFunctionMapEntry* e = lookup_rva(rva))
            {
                std::snprintf(buf, sizeof(buf), "+0x%llX",
                              static_cast<unsigned long long>(
                                  rva - e->start_rva));
                return e->name + buf;
            }
            std::snprintf(buf, sizeof(buf), "SoulcaliburVI.exe+0x%llX",
                          static_cast<unsigned long long>(rva));
            return buf;
        }

        bool loaded() const noexcept { return !m_entries.empty(); }
        const std::wstring& loaded_path() const noexcept
        {
            return m_loaded_path;
        }

    private:
        static bool parse_hex(const std::string& s, uintptr_t& out) noexcept
        {
            const char* p = s.c_str();
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
            char* tail = nullptr;
            const unsigned long long v = std::strtoull(p, &tail, 16);
            if (!tail || *tail != '\0') return false;
            out = static_cast<uintptr_t>(v);
            return true;
        }

        static bool read_file(const std::wstring& path,
                              std::string& out) noexcept
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
                return false;

            LARGE_INTEGER size{};
            if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0
                || size.QuadPart > 64ll * 1024ll * 1024ll)
            {
                CloseHandle(h);
                return false;
            }

            out.resize(static_cast<size_t>(size.QuadPart));
            DWORD got = 0;
            const BOOL ok = ReadFile(h, out.data(),
                                     static_cast<DWORD>(out.size()),
                                     &got, nullptr);
            CloseHandle(h);
            if (!ok) return false;
            out.resize(got);
            return true;
        }

        std::vector<ReplayFunctionMapEntry> m_entries;
        std::wstring m_loaded_path;
    };

    class ReplayTraceFields
    {
    public:
        ReplayTraceFields& string(const char* key, const char* value)
        {
            m_fields.emplace_back(std::string(key) + "\":\""
                                  + escape(value ? value : "") + "\"");
            return *this;
        }

        ReplayTraceFields& string(const char* key, const std::string& value)
        {
            m_fields.emplace_back(std::string(key) + "\":\""
                                  + escape(value) + "\"");
            return *this;
        }

        ReplayTraceFields& boolean(const char* key, bool value)
        {
            m_fields.emplace_back(std::string(key) + "\":"
                                  + (value ? "true" : "false"));
            return *this;
        }

        ReplayTraceFields& integer(const char* key, int64_t value)
        {
            char buf[64]{};
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(value));
            m_fields.emplace_back(std::string(key) + "\":" + buf);
            return *this;
        }

        ReplayTraceFields& uinteger(const char* key, uint64_t value)
        {
            char buf[64]{};
            std::snprintf(buf, sizeof(buf), "%llu",
                          static_cast<unsigned long long>(value));
            m_fields.emplace_back(std::string(key) + "\":" + buf);
            return *this;
        }

        ReplayTraceFields& real(const char* key, double value)
        {
            char buf[64]{};
            if (std::isfinite(value))
                std::snprintf(buf, sizeof(buf), "%.9g", value);
            else
                std::snprintf(buf, sizeof(buf), "null");
            m_fields.emplace_back(std::string(key) + "\":" + buf);
            return *this;
        }

        ReplayTraceFields& hex(const char* key, uintptr_t value)
        {
            char buf[64]{};
            std::snprintf(buf, sizeof(buf), "0x%llX",
                          static_cast<unsigned long long>(value));
            m_fields.emplace_back(std::string(key) + "\":\"" + buf + "\"");
            return *this;
        }

        ReplayTraceFields& hash(const char* key,
                                const void* data,
                                size_t size)
        {
            return hex(key, static_cast<uintptr_t>(fnv1a64(data, size)));
        }

        const std::vector<std::string>& fields() const noexcept
        {
            return m_fields;
        }

        static uint64_t fnv1a64(const void* data, size_t size) noexcept
        {
            const uint8_t* p = static_cast<const uint8_t*>(data);
            uint64_t h = 1469598103934665603ull;
            for (size_t i = 0; i < size; ++i)
            {
                h ^= p[i];
                h *= 1099511628211ull;
            }
            return h;
        }

    private:
        static std::string escape(const std::string& in)
        {
            std::string out;
            out.reserve(in.size() + 8);
            for (unsigned char c : in)
            {
                switch (c)
                {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8]{};
                        std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                        out += buf;
                    }
                    else
                    {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
                }
            }
            return out;
        }

        std::vector<std::string> m_fields;
    };

    class ReplayDebugTrace
    {
    public:
        static ReplayDebugTrace& instance() noexcept
        {
            static ReplayDebugTrace s_instance;
            return s_instance;
        }

        void set_enabled(bool enabled) noexcept
        {
            if (enabled)
            {
                m_enabled.store(true, std::memory_order_release);
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            m_enabled.store(false, std::memory_order_release);
            close_session_locked();
        }

        bool enabled() const noexcept
        {
            return m_enabled.load(std::memory_order_acquire);
        }

        void set_mirror_to_log(bool enabled) noexcept
        {
            m_mirror_to_log.store(enabled, std::memory_order_release);
        }

        bool mirror_to_log() const noexcept
        {
            return m_mirror_to_log.load(std::memory_order_acquire);
        }

        void set_verbose_slices(bool enabled) noexcept
        {
            m_verbose_slices.store(enabled, std::memory_order_release);
        }

        bool verbose_slices() const noexcept
        {
            return m_verbose_slices.load(std::memory_order_acquire);
        }

        bool open_new_session(const wchar_t* reason) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            close_session_locked();
            m_enabled.store(true, std::memory_order_release);
            load_function_map_locked();

            const std::wstring root = mod_root_dir();
            if (root.empty())
                return disable_open_failed_locked(L"mod root unavailable");
            std::wstring dir = root;
            dir += L"Saved\\";
            CreateDirectoryW(dir.c_str(), nullptr);
            dir += L"ReplayTrace\\";
            CreateDirectoryW(dir.c_str(), nullptr);

            m_current_path = dir + make_trace_filename();
            m_file = CreateFileW(m_current_path.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
            if (m_file == INVALID_HANDLE_VALUE)
                return disable_open_failed_locked(L"CreateFileW failed");

            ReplayTraceFields f;
            f.string("reason", narrow(reason ? reason : L"manual").c_str())
             .string("mod_root", narrow(root))
             .string("trace_path", narrow(m_current_path))
             .boolean("function_map_loaded", m_function_map.loaded())
             .string("function_map_path",
                     narrow(m_function_map.loaded_path()));
            write_event_locked("session_start", f);

            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayTrace] writing {}\n"),
                RC::to_generic_string(narrow(m_current_path)));
            return true;
        }

        void close_session() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            close_session_locked();
        }

        const std::wstring& current_path() const noexcept
        {
            return m_current_path;
        }

        std::string current_path_utf8() const
        {
            return narrow(m_current_path);
        }

        std::string current_mod_root_utf8() const
        {
            return narrow(mod_root_dir());
        }

        void event(const char* event_name,
                   const ReplayTraceFields& fields = {}) noexcept
        {
            if (!enabled()) return;
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!enabled()) return;
            if (m_file == INVALID_HANDLE_VALUE
                && !open_new_session_locked(L"auto"))
                return;
            write_event_locked(event_name, fields);
        }

        template<typename Job, typename Ctx>
        void seek_phase(const char* phase,
                        const Job& job,
                        const Ctx* ctx) noexcept
        {
            ReplayTraceFields f;
            add_job_fields(f, job);
            f.string("phase", phase ? phase : "?");
            if (ctx) add_context_fields(f, *ctx);
            event("sc6_seek_phase", f);
        }

        template<typename Failure, typename Job, typename Ctx>
        void failure(const char* phase,
                     Failure failure_value,
                     const Job* job,
                     const Ctx* ctx) noexcept
        {
            ReplayTraceFields f;
            f.string("phase", phase ? phase : "?")
             .integer("failure", static_cast<int64_t>(failure_value));
            if (job) add_job_fields(f, *job);
            if (ctx) add_context_fields(f, *ctx);
            event("sc6_failed", f);
        }

        std::string format_absolute_rip(uintptr_t rip) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            load_function_map_locked();
            return m_function_map.format_absolute_rip(
                NativeBinding::imageBase(), rip);
        }

        void add_fault_fields(ReplayTraceFields& f,
                              const NativeCallFault& fault) noexcept
        {
            f.boolean("faulted", fault.faulted)
             .hex("exception_code", fault.exception_code)
             .hex("exception_rip", fault.exception_address);
            const uintptr_t base = NativeBinding::imageBase();
            if (base && fault.exception_address >= base)
                f.hex("exception_rva", fault.exception_address - base);
            const std::string fn =
                format_absolute_rip(fault.exception_address);
            if (!fn.empty())
                f.string("exception_function", fn);
        }

    private:
        ReplayDebugTrace() = default;
        ~ReplayDebugTrace() { close_session(); }
        ReplayDebugTrace(const ReplayDebugTrace&) = delete;
        ReplayDebugTrace& operator=(const ReplayDebugTrace&) = delete;

        template<typename Job>
        static void add_job_fields(ReplayTraceFields& f,
                                   const Job& job)
        {
            f.integer("job_generation", job.generation)
             .integer("job_phase", static_cast<int>(job.phase))
             .integer("requested_seq", job.requested_seq)
             .integer("target_seq", job.target_seq)
             .integer("target_round", job.target_round)
             .integer("target_master", job.target_master)
             .integer("round_start_seq", job.round_start_seq)
             .integer("round_start_master", job.round_start_master)
             .integer("frames_advanced", job.frames_advanced)
             .integer("slices_serviced", job.slices_serviced)
             .integer("stall_count", job.stall_count)
             .integer("failure_code", static_cast<int>(job.failure))
             .string("label", job.label ? job.label : "?");
        }

        template<typename Ctx>
        static void add_context_fields(ReplayTraceFields& f,
                                       const Ctx& ctx)
        {
            f.integer("context_source", static_cast<int>(ctx.source))
             .boolean("context_readable", ctx.readable)
             .integer("context_failure", static_cast<int>(ctx.failure))
             .hex("wmp", ctx.world_mode_pump)
             .hex("bm", ctx.battle_manager)
             .hex("object_bm", ctx.object_registry_battle_manager)
             .hex("wmp_bm", ctx.world_mode_pump_battle_manager)
             .hex("sub_driver", ctx.sub_driver)
             .hex("input_log", ctx.input_log)
             .hex("replay_player", ctx.replay_player)
             .hex("state_reset_data", ctx.state_reset_data)
             .hex("interactive_replay", ctx.interactive_replay)
             .integer("total_rounds", ctx.total_rounds)
             .integer("current_round", ctx.current_round)
             .integer("input_master", ctx.input_master)
             .integer("battle_master", ctx.battle_master)
             .boolean("bm_ok", ctx.battle_manager_ok)
             .boolean("input_log_ok", ctx.input_log_ok)
             .boolean("replay_player_ok", ctx.replay_player_ok)
             .boolean("state_reset_data_ok", ctx.state_reset_data_ok)
             .boolean("interactive_replay_ok", ctx.interactive_replay_ok)
             .boolean("captured_round_reset_ok",
                      ctx.captured_round_reset_ok);
        }

        static std::wstring make_trace_filename()
        {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            wchar_t buf[128]{};
            std::swprintf(buf, 128,
                          L"replay_trace_%04u%02u%02u_%02u%02u%02u_pid%lu.jsonl",
                          st.wYear, st.wMonth, st.wDay, st.wHour,
                          st.wMinute, st.wSecond, GetCurrentProcessId());
            return buf;
        }

        static std::wstring dll_dir()
        {
            HMODULE h = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&ReplayDebugTrace::instance),
                    &h) || !h)
                return {};

            wchar_t buf[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(h, buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return {};
            wchar_t* last_slash = std::wcsrchr(buf, L'\\');
            if (!last_slash) return {};
            *(last_slash + 1) = L'\0';
            return buf;
        }

        static std::wstring mod_root_dir()
        {
            std::wstring p = dll_dir();
            if (p.empty()) return {};
            if (!p.ends_with(L"\\dlls\\") && !p.ends_with(L"\\dlls/"))
                return p;
            if (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))
                p.pop_back();
            wchar_t* last_slash = std::wcsrchr(p.data(), L'\\');
            if (!last_slash) return {};
            *(last_slash + 1) = L'\0';
            p.resize(std::wcslen(p.c_str()));
            return p;
        }

        static std::string narrow(const std::wstring& in)
        {
            if (in.empty()) return {};
            const int need = WideCharToMultiByte(
                CP_UTF8, 0, in.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (need <= 1) return {};
            std::string out(static_cast<size_t>(need - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, out.data(),
                                need, nullptr, nullptr);
            return out;
        }

        static std::string narrow(const wchar_t* in)
        {
            return narrow(std::wstring(in ? in : L""));
        }

        bool disable_open_failed_locked(const wchar_t* reason) noexcept
        {
            m_enabled.store(false, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[ReplayTrace] disabled - {}\n"),
                RC::to_generic_string(narrow(reason ? reason : L"open failed")));
            return false;
        }

        bool open_new_session_locked(const wchar_t* reason) noexcept
        {
            // Caller already holds m_mutex; duplicate the open path without
            // taking the lock again.
            close_session_locked();
            load_function_map_locked();

            const std::wstring root = mod_root_dir();
            if (root.empty())
                return disable_open_failed_locked(L"mod root unavailable");
            std::wstring dir = root;
            dir += L"Saved\\";
            CreateDirectoryW(dir.c_str(), nullptr);
            dir += L"ReplayTrace\\";
            CreateDirectoryW(dir.c_str(), nullptr);

            m_current_path = dir + make_trace_filename();
            m_file = CreateFileW(m_current_path.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
            if (m_file == INVALID_HANDLE_VALUE)
                return disable_open_failed_locked(L"CreateFileW failed");

            ReplayTraceFields f;
            f.string("reason", narrow(reason ? reason : L"auto"))
             .string("mod_root", narrow(root))
             .string("trace_path", narrow(m_current_path))
             .boolean("function_map_loaded", m_function_map.loaded())
             .string("function_map_path",
                     narrow(m_function_map.loaded_path()));
            write_event_locked("session_start", f);
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[ReplayTrace] writing {}\n"),
                RC::to_generic_string(narrow(m_current_path)));
            return true;
        }

        void close_session_locked() noexcept
        {
            if (m_file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_file);
                m_file = INVALID_HANDLE_VALUE;
            }
        }

        void load_function_map_locked() noexcept
        {
            if (m_function_map_loaded_attempted)
                return;
            m_function_map_loaded_attempted = true;

            const std::wstring dll = dll_dir();
            if (!dll.empty())
            {
                std::wstring p = dll + L"SoulcaliburVI_function_map.tsv";
                if (m_function_map.load_tsv(p))
                    return;
            }

            const std::wstring root = mod_root_dir();
            if (!root.empty())
            {
                std::wstring p = root + L"SoulcaliburVI_function_map.tsv";
                (void)m_function_map.load_tsv(p);
            }
        }

        void write_event_locked(const char* event_name,
                                const ReplayTraceFields& fields) noexcept
        {
            LARGE_INTEGER qpc{};
            QueryPerformanceCounter(&qpc);
            std::string line;
            line.reserve(512);
            line += "{\"ts_qpc\":";
            line += std::to_string(qpc.QuadPart);
            line += ",\"thread_id\":";
            line += std::to_string(GetCurrentThreadId());
            line += ",\"pid\":";
            line += std::to_string(GetCurrentProcessId());
            line += ",\"process_start_marker\":";
            line += std::to_string(process_start_marker());
            line += ",\"event\":\"";
            line += event_name ? event_name : "?";
            line += "\",\"build\":\"replay-accuracy-v13fl\"";
            line += ",\"image_base\":\"";
            char buf[64]{};
            std::snprintf(buf, sizeof(buf), "0x%llX",
                          static_cast<unsigned long long>(
                              NativeBinding::imageBase()));
            line += buf;
            line += "\"";

            for (const std::string& f : fields.fields())
            {
                line += ",\"";
                line += f;
            }
            line += "}\n";

            if (m_file != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                (void)WriteFile(m_file, line.data(),
                                static_cast<DWORD>(line.size()),
                                &written, nullptr);
            }
            if (m_mirror_to_log.load(std::memory_order_acquire))
            {
                RC::Output::send<RC::LogLevel::Default>(STR(
                    "[ReplayTrace] {}\n"),
                    RC::to_generic_string(line));
            }
        }

        static uint64_t process_start_marker() noexcept
        {
            FILETIME create_time {};
            FILETIME exit_time {};
            FILETIME kernel_time {};
            FILETIME user_time {};
            if (!GetProcessTimes(
                    GetCurrentProcess(),
                    &create_time,
                    &exit_time,
                    &kernel_time,
                    &user_time))
            {
                return 0;
            }

            ULARGE_INTEGER value {};
            value.LowPart = create_time.dwLowDateTime;
            value.HighPart = create_time.dwHighDateTime;
            return value.QuadPart;
        }

        std::atomic<bool> m_enabled {false};
        std::atomic<bool> m_mirror_to_log {false};
        std::atomic<bool> m_verbose_slices {false};
        mutable std::mutex m_mutex;
        HANDLE m_file {INVALID_HANDLE_VALUE};
        std::wstring m_current_path;
        ReplayFunctionMap m_function_map;
        bool m_function_map_loaded_attempted {false};
    };
}
