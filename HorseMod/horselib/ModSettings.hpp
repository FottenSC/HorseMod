// ============================================================================
// Horse::ModSettings — persist a handful of mod state (checkbox toggles,
// sliders, thickness, etc.) between game sessions.
//
// File format
// -----------
//   A plain text file at  <mod_folder>/settings.cfg, siblings of
//   enabled.txt and the dlls/ directory:
//
//       # HorseMod settings — auto-saved.  Edit while the game is closed.
//       show_p1_hurt=0
//       show_p2_hurt=1
//       thickness=1.5000
//       line_batcher_slot=2
//
//   Lines starting with '#' or ';' are comments.  Blank lines are
//   ignored.  Whitespace around key and value is trimmed.  Keys are
//   case-sensitive ASCII.  Unknown keys are preserved on save so a
//   future mod version can add settings without stomping user data.
//
// Design choices
// --------------
//   * File location is derived from our own DLL's module path via
//     GetModuleHandleExW(FROM_ADDRESS) + GetModuleFileNameW, then
//     backing up one directory from dlls/.  This works regardless of
//     the user's mod-folder name (we observed "HorseLab" in the wild,
//     but that's just the folder name under ue4ss/Mods/).
//
//   * Values are stored internally as std::string for simplicity;
//     typed get_bool / get_int / get_float do the parse, with a
//     default fallback if the key is missing or unparseable.  Setters
//     write the canonical string form and mark a dirty flag.
//
//   * Saving is explicit: HorseMod's on_update ticker calls
//     save_if_dirty() every ~120 frames (~2s at 60 FPS), and the
//     dtor does one last save.  We don't auto-save on every set()
//     because UI-driven setters can fire multiple times per frame
//     when the user drags a slider — the dirty-flag + periodic save
//     batches that into one disk write per ~2s max.
//
//   * Everything is mutex-guarded so the UI thread (calling get/set
//     from render_*_tab) and the cockpit thread (calling
//     save_if_dirty from on_update) can interleave freely.
//
// Not for secrets
// ---------------
// This file is plain text and trivially tamperable.  Don't put
// credentials or anti-cheat-bypass flags in it.  For HorseMod's
// use-case (remember "I want P2 hurtboxes visible on launch") that's
// fine.
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Horse
{
    class ModSettings
    {
    public:
        static ModSettings& instance()
        {
            static ModSettings s;
            return s;
        }

        // Read the settings.cfg file into the in-memory map.  Safe to
        // call before any get_* — if the file doesn't exist yet
        // (first-run) the map stays empty and all get_*s return
        // their default argument.  Safe to call multiple times; the
        // map is rebuilt each time.
        void load()
        {
            std::lock_guard g(m_mutex);
            m_values.clear();

            const std::wstring path = settings_path();
            if (path.empty())
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[ModSettings] could not derive settings path — "
                        "settings won't persist this session.\n"));
                return;
            }

            std::ifstream f(path);
            if (!f.is_open())
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[ModSettings] no settings.cfg yet (first run) at {}\n"),
                    path);
                return;
            }

            std::string line;
            int loaded = 0;
            while (std::getline(f, line))
            {
                // Trim trailing \r for CRLF files.
                while (!line.empty() && (line.back() == '\r' ||
                                          line.back() == '\n'))
                    line.pop_back();
                // Skip blank / whitespace-only.
                const auto first_non_ws = line.find_first_not_of(" \t");
                if (first_non_ws == std::string::npos) continue;
                // Comments.
                const char c0 = line[first_non_ws];
                if (c0 == '#' || c0 == ';') continue;
                // Parse key=value.
                const auto eq = line.find('=', first_non_ws);
                if (eq == std::string::npos) continue;
                std::string key = line.substr(first_non_ws, eq - first_non_ws);
                std::string val = line.substr(eq + 1);
                // Trim key right.
                while (!key.empty() &&
                       (key.back() == ' ' || key.back() == '\t'))
                    key.pop_back();
                // Trim value both sides.
                const auto v_first = val.find_first_not_of(" \t");
                if (v_first == std::string::npos) val.clear();
                else                               val = val.substr(v_first);
                while (!val.empty() &&
                       (val.back() == ' ' || val.back() == '\t'))
                    val.pop_back();
                if (key.empty()) continue;
                m_values.emplace(std::move(key), std::move(val));
                ++loaded;
            }

            RC::Output::send<RC::LogLevel::Default>(
                STR("[ModSettings] loaded {} entries from {}\n"),
                loaded, path);
        }

        // Periodic save: if nothing has changed since the last save,
        // this is a cheap std::atomic exchange + early return.
        void save_if_dirty()
        {
            if (!m_dirty.exchange(false, std::memory_order_acq_rel))
            {
                return;
            }
            save();
        }

        // Force a save (e.g. from the dtor).  Bypasses the dirty
        // flag — writes every entry regardless.
        void save()
        {
            std::lock_guard g(m_mutex);

            const std::wstring path = settings_path();
            if (path.empty()) return;

            std::ofstream f(path, std::ios::trunc);
            if (!f.is_open())
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[ModSettings] failed to open {} for writing\n"),
                    path);
                return;
            }

            f << "# HorseMod settings - auto-saved.  Safe to hand-edit\n"
              << "# while the game is closed; any unknown keys will be\n"
              << "# preserved across mod versions.\n";

            // Deterministic output order: sort keys so diffs are
            // small across saves.  Keys are short ASCII, so the cost
            // is negligible.
            std::vector<std::string> keys;
            keys.reserve(m_values.size());
            for (auto const& [k, v] : m_values) keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            for (auto const& k : keys)
            {
                f << k << '=' << m_values[k] << '\n';
            }
        }

        // -------- Typed getters ------------------------------------------

        bool get_bool(std::string_view key, bool def) const
        {
            std::lock_guard g(m_mutex);
            const auto it = m_values.find(std::string(key));
            if (it == m_values.end()) return def;
            const auto& v = it->second;
            if (v == "1" || v == "true" || v == "TRUE" || v == "True") return true;
            if (v == "0" || v == "false" || v == "FALSE" || v == "False") return false;
            return def;
        }

        int get_int(std::string_view key, int def) const
        {
            std::lock_guard g(m_mutex);
            const auto it = m_values.find(std::string(key));
            if (it == m_values.end()) return def;
            int v = def;
            const char* first = it->second.data();
            const char* last  = first + it->second.size();
            auto [p, ec] = std::from_chars(first, last, v);
            return (ec == std::errc()) ? v : def;
        }

        float get_float(std::string_view key, float def) const
        {
            std::lock_guard g(m_mutex);
            const auto it = m_values.find(std::string(key));
            if (it == m_values.end()) return def;
            try
            {
                return std::stof(it->second);
            }
            catch (...)
            {
                return def;
            }
        }

        // -------- Typed setters ------------------------------------------
        //
        // Setters compare against the cached value and only raise the
        // dirty flag on a real change.  This means UI code can call
        // set(...) every frame unconditionally (easy pattern) without
        // causing a disk write every second.

        void set(std::string_view key, bool v)
        {
            const char* s = v ? "1" : "0";
            set_raw(key, s);
        }

        void set(std::string_view key, int v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d", v);
            set_raw(key, buf);
        }

        void set(std::string_view key, float v)
        {
            char buf[32];
            // 4 decimal places is enough for thickness / speed /
            // look sliders and doesn't leak insignificant jitter
            // into the file.
            std::snprintf(buf, sizeof(buf), "%.4f", v);
            set_raw(key, buf);
        }

    private:
        ModSettings() = default;
        ~ModSettings() = default;
        ModSettings(const ModSettings&)            = delete;
        ModSettings& operator=(const ModSettings&) = delete;

        void set_raw(std::string_view key, const char* str)
        {
            std::lock_guard g(m_mutex);
            auto it = m_values.find(std::string(key));
            if (it == m_values.end())
            {
                m_values.emplace(std::string(key), std::string(str));
                m_dirty.store(true, std::memory_order_release);
            }
            else if (it->second != str)
            {
                it->second = str;
                m_dirty.store(true, std::memory_order_release);
            }
        }

        // Derive the settings.cfg path from OUR dll's location via
        // GetModuleHandleExW(FROM_ADDRESS).  Cached on first call
        // since the path is immutable for the process lifetime.
        //
        // Layout assumption: our DLL is at
        //   <mod_folder>/dlls/main.dll
        // and we want
        //   <mod_folder>/settings.cfg
        // so we strip two path components from the DLL path, then
        // append the filename.
        std::wstring settings_path() const
        {
            static std::wstring s_cached;
            if (!s_cached.empty()) return s_cached;

            HMODULE h = nullptr;
            if (!GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&ModSettings::instance),
                    &h) || !h)
            {
                return {};
            }

            wchar_t buf[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(h, buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return {};

            // buf = ...\<mod_folder>\dlls\<dllname>.dll
            // Strip the filename: drop everything after the final backslash.
            wchar_t* last_slash = wcsrchr(buf, L'\\');
            if (!last_slash) return {};
            *last_slash = L'\0';
            // Now buf = ...\<mod_folder>\dlls
            // Strip the "dlls" component.
            last_slash = wcsrchr(buf, L'\\');
            if (!last_slash) return {};
            *(last_slash + 1) = L'\0';
            // Now buf = ...\<mod_folder>\  (trailing backslash kept)

            std::wstring p(buf);
            p += L"settings.cfg";
            s_cached = std::move(p);
            return s_cached;
        }

        mutable std::mutex                                 m_mutex;
        std::unordered_map<std::string, std::string>       m_values;
        std::atomic<bool>                                  m_dirty{false};
    };
}
