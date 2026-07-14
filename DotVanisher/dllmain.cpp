// ============================================================================
// DotVanisher
//
// Standalone UE4SS C++ mod for Soulcalibur VI spectator loads.
//
// Ghidra notes:
//   HandleHostTickWatchEventQueues @ SoulcaliburVI.exe+0x2E613A0
//   pHostSysState+0xB0 : pending watch spectator list pointer
//   pHostSysState+0xB8 : pending watch spectator count
//   pHostSysState+0xC0 : host watch timeout timer
//
// The vanilla function forces a host-side watch-end when the timeout timer
// expires while spectators are still pending. Slow HDD loads and heavier stages
// can make that timer expire even though the player match is fine. DotVanisher
// gives pending watch spectators a bounded 90-second grace window by offsetting
// the timeout timer before forwarding to the original tick function.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>

#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <polyhook2/Detour/x64Detour.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace DotVanisher
{
    constexpr uintptr_t kHandleHostTickWatchEventQueuesRva = 0x2E613A0;
    constexpr ptrdiff_t kWatchListPointerOffset = 0xB0;
    constexpr ptrdiff_t kWatchCountOffset = 0xB8;
    constexpr ptrdiff_t kWatchTimeoutTimerOffset = 0xC0;
    constexpr int64_t kMaxReasonableWatchCount = 16;
    constexpr uint64_t kGraceMs = 90'000;
    constexpr uint64_t kExpectedExeSize = 71'737'344;
    constexpr float kMaxDeltaCompensationSeconds = 120.0f;

    constexpr std::array<uint8_t, 32> kExpectedExeSha256{
        0xF8, 0x90, 0x4E, 0x4B, 0x04, 0xBC, 0xA3, 0xB4,
        0x7B, 0xC5, 0x2A, 0x68, 0x3F, 0x61, 0x90, 0x36,
        0x5D, 0x2E, 0xB8, 0x9E, 0xE8, 0xF4, 0x4F, 0x80,
        0x72, 0x75, 0x9E, 0x9C, 0x5E, 0x04, 0xA5, 0x53,
    };

    constexpr std::array<uint8_t, 32> kExpectedTargetPrologue{
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48,
        0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8D, 0x6C, 0x24, 0x90, 0x48, 0x81, 0xEC,
    };

    bool nt_success(NTSTATUS status) noexcept
    {
        return status >= 0;
    }

    bool read_i64_seh(const void* address, int64_t& out) noexcept
    {
        __try
        {
            out = *reinterpret_cast<const volatile int64_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool read_uintptr_seh(const void* address, uintptr_t& out) noexcept
    {
        __try
        {
            out = static_cast<uintptr_t>(
                *reinterpret_cast<const volatile uint64_t*>(address));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool read_float_seh(const void* address, float& out) noexcept
    {
        __try
        {
            out = *reinterpret_cast<const volatile float*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool read_bytes_seh(const void* address, uint8_t* out, size_t size) noexcept
    {
        __try
        {
            const auto* src = reinterpret_cast<const volatile uint8_t*>(address);
            for (size_t i = 0; i < size; ++i)
            {
                out[i] = src[i];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool write_float_seh(void* address, float value) noexcept
    {
        __try
        {
            *reinterpret_cast<volatile float*>(address) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float observed_positive_delta(float flDeltaSeconds) noexcept
    {
        if (!std::isfinite(flDeltaSeconds) || flDeltaSeconds < 0.0f)
        {
            return 0.0f;
        }
        return flDeltaSeconds;
    }

    float timer_compensation_for_delta(float flDeltaSeconds) noexcept
    {
        const float delta = observed_positive_delta(flDeltaSeconds);
        return -(std::min)(delta, kMaxDeltaCompensationSeconds);
    }

    bool hash_file_sha256(HANDLE file, std::array<uint8_t, 32>& out)
    {
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD object_length = 0;
        DWORD result_length = 0;
        std::unique_ptr<uint8_t[]> hash_object{};
        bool ok = false;

        do
        {
            if (!nt_success(::BCryptOpenAlgorithmProvider(
                    &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
            {
                break;
            }

            if (!nt_success(::BCryptGetProperty(
                    alg,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&object_length),
                    sizeof(object_length),
                    &result_length,
                    0)))
            {
                break;
            }

            hash_object = std::make_unique<uint8_t[]>(object_length);
            if (!nt_success(::BCryptCreateHash(
                    alg, &hash, hash_object.get(), object_length, nullptr, 0, 0)))
            {
                break;
            }

            LARGE_INTEGER zero{};
            if (!::SetFilePointerEx(file, zero, nullptr, FILE_BEGIN))
            {
                break;
            }

            std::array<uint8_t, 64 * 1024> buffer{};
            for (;;)
            {
                DWORD bytes_read = 0;
                if (!::ReadFile(
                        file,
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &bytes_read,
                        nullptr))
                {
                    break;
                }

                if (bytes_read == 0)
                {
                    ok = nt_success(::BCryptFinishHash(
                        hash,
                        out.data(),
                        static_cast<ULONG>(out.size()),
                        0));
                    break;
                }

                if (!nt_success(::BCryptHashData(
                        hash,
                        buffer.data(),
                        bytes_read,
                        0)))
                {
                    break;
                }
            }
        } while (false);

        if (hash)
        {
            ::BCryptDestroyHash(hash);
        }
        if (alg)
        {
            ::BCryptCloseAlgorithmProvider(alg, 0);
        }
        return ok;
    }

    bool verify_exe_file(const wchar_t* exe_path)
    {
        HANDLE file = ::CreateFileW(
            exe_path,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[DotVanisher] failed to open SoulcaliburVI.exe for "
                    "binary verification (GetLastError={})\n"),
                static_cast<uint32_t>(::GetLastError()));
            return false;
        }

        bool ok = false;
        do
        {
            LARGE_INTEGER file_size{};
            if (!::GetFileSizeEx(file, &file_size))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] failed to read SoulcaliburVI.exe size "
                        "(GetLastError={})\n"),
                    static_cast<uint32_t>(::GetLastError()));
                break;
            }

            if (static_cast<uint64_t>(file_size.QuadPart) != kExpectedExeSize)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] unsupported SoulcaliburVI.exe size "
                        "(actual={}, expected={}); hook disabled\n"),
                    static_cast<uint64_t>(file_size.QuadPart),
                    kExpectedExeSize);
                break;
            }

            std::array<uint8_t, 32> actual_sha{};
            if (!hash_file_sha256(file, actual_sha))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] failed to hash SoulcaliburVI.exe; "
                        "hook disabled\n"));
                break;
            }

            if (actual_sha != kExpectedExeSha256)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] unsupported SoulcaliburVI.exe SHA-256; "
                        "hook disabled\n"));
                break;
            }

            ok = true;
        } while (false);

        ::CloseHandle(file);
        return ok;
    }

    bool verify_target_image(HMODULE exe_module, uintptr_t target)
    {
        std::array<wchar_t, 32768> exe_path{};
        const DWORD path_len = ::GetModuleFileNameW(
            exe_module, exe_path.data(), static_cast<DWORD>(exe_path.size()));
        if (path_len == 0 || path_len >= exe_path.size())
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[DotVanisher] failed to resolve SoulcaliburVI.exe path "
                    "for binary verification (GetLastError={})\n"),
                static_cast<uint32_t>(::GetLastError()));
            return false;
        }

        if (!verify_exe_file(exe_path.data()))
        {
            return false;
        }

        std::array<uint8_t, kExpectedTargetPrologue.size()> actual_prologue{};
        if (!read_bytes_seh(
                reinterpret_cast<const void*>(target),
                actual_prologue.data(),
                actual_prologue.size()))
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[DotVanisher] failed to read target prologue at 0x{:X}; "
                    "hook disabled\n"),
                target);
            return false;
        }

        if (actual_prologue != kExpectedTargetPrologue)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[DotVanisher] target prologue mismatch at 0x{:X}; "
                    "SC6 build or hook conflict is unsupported, hook disabled\n"),
                target);
            return false;
        }

        RC::Output::send<RC::LogLevel::Default>(
            STR("[DotVanisher] target binary verified "
                "(size={}, rva=0x{:X})\n"),
            kExpectedExeSize,
            kHandleHostTickWatchEventQueuesRva);
        return true;
    }

    class WatchTimeoutHook
    {
    public:
        using OriginalFn = bool(__fastcall*)(uint8_t* pHostSysState, float flDeltaSeconds);

        static WatchTimeoutHook& instance()
        {
            static WatchTimeoutHook s;
            return s;
        }

        bool install()
        {
            if (m_installed.load(std::memory_order_acquire))
            {
                return true;
            }

            HMODULE exe_module = ::GetModuleHandleW(L"SoulcaliburVI.exe");
            if (!exe_module)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] SoulcaliburVI.exe image base not found; "
                        "cannot install spectator timeout hook\n"));
                return false;
            }

            const uintptr_t image_base = reinterpret_cast<uintptr_t>(exe_module);
            const uintptr_t target = image_base + kHandleHostTickWatchEventQueuesRva;
            if (!verify_target_image(exe_module, target))
            {
                return false;
            }

            if (!pin_own_module())
            {
                return false;
            }

            m_trampoline = 0;
            m_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(&WatchTimeoutHook::detour),
                &m_trampoline);

            if (!m_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] x64Detour::hook() failed for "
                        "HandleHostTickWatchEventQueues "
                        "(target=0x{:X}, image_base=0x{:X}, rva=0x{:X})\n"),
                    target,
                    image_base,
                    kHandleHostTickWatchEventQueuesRva);
                m_detour.reset();
                m_trampoline = 0;
                return false;
            }

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[DotVanisher] spectator timeout hook installed "
                    "(target=0x{:X}, trampoline=0x{:X})\n"),
                target,
                static_cast<uintptr_t>(m_trampoline));
            return true;
        }

    private:
        WatchTimeoutHook() = default;
        ~WatchTimeoutHook() { uninstall_for_process_exit(); }
        WatchTimeoutHook(const WatchTimeoutHook&) = delete;
        WatchTimeoutHook& operator=(const WatchTimeoutHook&) = delete;

        bool pin_own_module()
        {
            if (m_module_pinned.load(std::memory_order_acquire))
            {
                return true;
            }

            HMODULE self_module = nullptr;
            const auto self_address = reinterpret_cast<LPCWSTR>(
                reinterpret_cast<uintptr_t>(&WatchTimeoutHook::detour));
            if (!::GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_PIN,
                    self_address,
                    &self_module))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] failed to pin DotVanisher module; "
                        "hook disabled (GetLastError={})\n"),
                    static_cast<uint32_t>(::GetLastError()));
                return false;
            }

            m_module_pinned.store(true, std::memory_order_release);
            return true;
        }

        void uninstall_for_process_exit()
        {
            if (!m_installed.exchange(false, std::memory_order_acq_rel))
            {
                return;
            }

            if (m_detour)
            {
                m_detour->unHook();
                m_detour.reset();
            }

            m_trampoline = 0;
            clear_epoch_state(false);

            RC::Output::send<RC::LogLevel::Default>(
                STR("[DotVanisher] spectator timeout hook uninstalled "
                    "during process teardown\n"));
        }

        static bool __fastcall detour(uint8_t* pHostSysState, float flDeltaSeconds)
        {
            WatchTimeoutHook& self = instance();
            self.maybe_suppress_timeout(pHostSysState, flDeltaSeconds);

            const auto original = reinterpret_cast<OriginalFn>(self.m_trampoline);
            if (!original)
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[DotVanisher] missing trampoline; allowing vanilla tick "
                        "to fail closed\n"));
                return false;
            }

            return original(pHostSysState, flDeltaSeconds);
        }

        void maybe_suppress_timeout(uint8_t* pHostSysState, float flDeltaSeconds)
        {
            const uint64_t now_ms = static_cast<uint64_t>(::GetTickCount64());
            if (!pHostSysState)
            {
                clear_epoch_state(true, now_ms);
                return;
            }

            uintptr_t watch_list = 0;
            int64_t watch_count = 0;
            float timer_before = 0.0f;
            if (!read_uintptr_seh(pHostSysState + kWatchListPointerOffset, watch_list) ||
                !read_i64_seh(pHostSysState + kWatchCountOffset, watch_count) ||
                !read_float_seh(pHostSysState + kWatchTimeoutTimerOffset, timer_before))
            {
                clear_epoch_state(true, now_ms);
                if (!m_read_failure_logged.exchange(true, std::memory_order_acq_rel))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[DotVanisher] failed to read watch state fields; "
                            "leaving vanilla timeout untouched\n"));
                }
                return;
            }

            if (watch_count == 0)
            {
                clear_epoch_state(true, now_ms);
                return;
            }

            if (watch_count < 0 || watch_count > kMaxReasonableWatchCount)
            {
                clear_epoch_state(true, now_ms);
                if (!m_range_failure_logged.exchange(true, std::memory_order_acq_rel))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[DotVanisher] unreasonable watch count {}; "
                            "leaving vanilla timeout untouched\n"),
                        watch_count);
                }
                return;
            }

            const uintptr_t host_state = reinterpret_cast<uintptr_t>(pHostSysState);
            if (m_epoch_start_ms == 0 ||
                m_epoch_host_state != host_state ||
                m_epoch_watch_list != watch_list)
            {
                begin_epoch(
                    now_ms,
                    host_state,
                    watch_list,
                    watch_count,
                    flDeltaSeconds,
                    timer_before);
            }
            else
            {
                update_epoch_stats(watch_count, flDeltaSeconds, timer_before);
            }

            const uint64_t elapsed_ms = now_ms - m_epoch_start_ms;
            if (elapsed_ms < kGraceMs)
            {
                const float timer_write = timer_compensation_for_delta(flDeltaSeconds);
                if (!write_float_seh(
                        pHostSysState + kWatchTimeoutTimerOffset,
                        timer_write))
                {
                    clear_epoch_state(true, now_ms);
                    if (!m_write_failure_logged.exchange(true, std::memory_order_acq_rel))
                    {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[DotVanisher] failed to reset watch timeout timer "
                                "at +0x{:X}; vanilla timeout remains active\n"),
                            static_cast<uintptr_t>(kWatchTimeoutTimerOffset));
                    }
                }
                return;
            }

            if (!m_grace_expired_logged)
            {
                m_grace_expired_logged = true;
                m_epoch_summary_logged = true;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[DotVanisher] spectator timeout grace expired "
                        "(elapsed_ms={}, state=0x{:X}, watch_list=0x{:X}, "
                        "last_count={}, max_delta={}, max_timer_before={}); "
                        "vanilla cleanup allowed\n"),
                    elapsed_ms,
                    m_epoch_host_state,
                    m_epoch_watch_list,
                    m_epoch_last_count,
                    m_epoch_max_delta,
                    m_epoch_max_timer_before);
            }
        }

        void begin_epoch(
            uint64_t now_ms,
            uintptr_t host_state,
            uintptr_t watch_list,
            int64_t watch_count,
            float flDeltaSeconds,
            float timer_before)
        {
            clear_epoch_state(true, now_ms);

            m_epoch_start_ms = now_ms;
            m_epoch_host_state = host_state;
            m_epoch_watch_list = watch_list;
            m_epoch_last_count = watch_count;
            m_epoch_max_delta = observed_positive_delta(flDeltaSeconds);
            m_epoch_max_timer_before = observed_positive_delta(timer_before);
            m_grace_expired_logged = false;
            m_epoch_summary_logged = false;

            RC::Output::send<RC::LogLevel::Default>(
                STR("[DotVanisher] spectator timeout epoch started "
                    "(state=0x{:X}, watch_list=0x{:X}, watch_count={}, "
                    "delta={}, timer_before={}, grace_ms={})\n"),
                host_state,
                watch_list,
                watch_count,
                flDeltaSeconds,
                timer_before,
                kGraceMs);
        }

        void update_epoch_stats(
            int64_t watch_count,
            float flDeltaSeconds,
            float timer_before) noexcept
        {
            m_epoch_last_count = watch_count;
            m_epoch_max_delta = (std::max)(
                m_epoch_max_delta,
                observed_positive_delta(flDeltaSeconds));
            m_epoch_max_timer_before = (std::max)(
                m_epoch_max_timer_before,
                observed_positive_delta(timer_before));
        }

        void log_epoch_summary(uint64_t now_ms)
        {
            if (m_epoch_start_ms == 0 || m_epoch_summary_logged)
            {
                return;
            }

            m_epoch_summary_logged = true;
            RC::Output::send<RC::LogLevel::Default>(
                STR("[DotVanisher] spectator timeout epoch ended "
                    "(elapsed_ms={}, state=0x{:X}, watch_list=0x{:X}, "
                    "last_count={}, max_delta={}, max_timer_before={})\n"),
                now_ms - m_epoch_start_ms,
                m_epoch_host_state,
                m_epoch_watch_list,
                m_epoch_last_count,
                m_epoch_max_delta,
                m_epoch_max_timer_before);
        }

        void clear_epoch_state(bool log_summary, uint64_t now_ms = 0)
        {
            if (log_summary)
            {
                log_epoch_summary(now_ms);
            }

            m_epoch_start_ms = 0;
            m_epoch_host_state = 0;
            m_epoch_watch_list = 0;
            m_epoch_last_count = 0;
            m_epoch_max_delta = 0.0f;
            m_epoch_max_timer_before = 0.0f;
            m_grace_expired_logged = false;
            m_epoch_summary_logged = false;
        }

        std::unique_ptr<PLH::x64Detour> m_detour{};
        uint64_t m_trampoline = 0;
        std::atomic<bool> m_installed{false};
        std::atomic<bool> m_module_pinned{false};
        std::atomic<bool> m_read_failure_logged{false};
        std::atomic<bool> m_write_failure_logged{false};
        std::atomic<bool> m_range_failure_logged{false};
        uint64_t m_epoch_start_ms = 0;
        uintptr_t m_epoch_host_state = 0;
        uintptr_t m_epoch_watch_list = 0;
        int64_t m_epoch_last_count = 0;
        float m_epoch_max_delta = 0.0f;
        float m_epoch_max_timer_before = 0.0f;
        bool m_grace_expired_logged = false;
        bool m_epoch_summary_logged = false;
    };

    class Mod final : public RC::CppUserModBase
    {
    public:
        Mod() : CppUserModBase()
        {
            ModName = STR("DotVanisher");
            ModVersion = STR("0.1.0");
            ModDescription = STR("Softens SC6 host spectator timeout during slow match loads.");
            ModAuthors = STR("HorseMod contributors");
        }

        // DotVanisher pins its DLL and keeps the native detour process-lifetime;
        // UE4SS mod-object deletion must not unhook code that may be in flight.
        ~Mod() override = default;

        auto on_unreal_init() -> void override
        {
            WatchTimeoutHook::instance().install();
        }
    };
} // namespace DotVanisher

#define DOT_VANISHER_API __declspec(dllexport)

extern "C"
{
    DOT_VANISHER_API RC::CppUserModBase* start_mod()
    {
        return new DotVanisher::Mod();
    }

    DOT_VANISHER_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
