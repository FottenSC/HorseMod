#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Winhttp.h>
#include <TlHelp32.h>
#include <shellapi.h>

#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
    constexpr size_t kMaxReplayBytes = 64ull * 1024ull * 1024ull;
    constexpr wchar_t kSteamRunGameUrl[] = L"steam://rungameid/544750";

    std::wstring dirname(std::wstring path)
    {
        while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
            path.pop_back();
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return {};
        path.resize(slash + 1);
        return path;
    }

    std::wstring module_path()
    {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return {};
        return buf;
    }

    bool ensure_dir(const std::wstring& path)
    {
        if (path.empty()) return false;
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
            return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return CreateDirectoryW(path.c_str(), nullptr) ||
               GetLastError() == ERROR_ALREADY_EXISTS;
    }

    std::wstring local_appdata_log_path()
    {
        wchar_t base[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        std::wstring dir = base;
        if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
        dir += L"HorseMod\\";
        ensure_dir(dir);
        return dir + L"ReplayLauncher.log";
    }

    void log_line(const wchar_t* fmt, ...)
    {
        const std::wstring path = local_appdata_log_path();
        if (path.empty()) return;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") != 0 || !f)
            return;

        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::fwprintf(f, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
                      st.wYear, st.wMonth, st.wDay,
                      st.wHour, st.wMinute, st.wSecond);
        va_list args;
        va_start(args, fmt);
        std::vfwprintf(f, fmt, args);
        va_end(args);
        std::fwprintf(f, L"\n");
        std::fclose(f);
    }

    [[noreturn]] void fail(const std::wstring& message)
    {
        log_line(L"error: %s", message.c_str());
        MessageBoxW(nullptr, message.c_str(), L"HorseMod replay link",
                    MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }

    std::wstring utf8_to_wide(const std::string& in)
    {
        if (in.empty()) return {};
        const int need = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, in.data(),
            static_cast<int>(in.size()), nullptr, 0);
        if (need <= 0) return {};
        std::wstring out(static_cast<size_t>(need), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(),
                            static_cast<int>(in.size()), &out[0], need);
        return out;
    }

    std::string wide_to_utf8(const std::wstring& in)
    {
        if (in.empty()) return {};
        const int need = WideCharToMultiByte(
            CP_UTF8, 0, in.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (need <= 1) return {};
        std::string out(static_cast<size_t>(need - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, in.c_str(), -1, &out[0], need,
                            nullptr, nullptr);
        return out;
    }

    int hex_value(wchar_t c)
    {
        if (c >= L'0' && c <= L'9') return static_cast<int>(c - L'0');
        if (c >= L'a' && c <= L'f') return static_cast<int>(c - L'a' + 10);
        if (c >= L'A' && c <= L'F') return static_cast<int>(c - L'A' + 10);
        return -1;
    }

    bool percent_decode_utf8(const std::wstring& in, std::wstring& out)
    {
        std::string bytes;
        bytes.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == L'%')
            {
                if (i + 2 >= in.size()) return false;
                const int hi = hex_value(in[i + 1]);
                const int lo = hex_value(in[i + 2]);
                if (hi < 0 || lo < 0) return false;
                bytes.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            }
            else if (in[i] == L'+')
            {
                bytes.push_back(' ');
            }
            else if (in[i] <= 0x7f)
            {
                bytes.push_back(static_cast<char>(in[i]));
            }
            else
            {
                return false;
            }
        }
        out = utf8_to_wide(bytes);
        return !out.empty() || bytes.empty();
    }

    bool starts_with_ci(const std::wstring& text, const wchar_t* prefix)
    {
        for (size_t i = 0; prefix[i]; ++i)
        {
            if (i >= text.size()) return false;
            if (std::towlower(text[i]) != std::towlower(prefix[i]))
                return false;
        }
        return true;
    }

    bool valid_ugc(const std::wstring& ugc)
    {
        if (ugc.empty() || ugc.size() > 20) return false;
        for (wchar_t c : ugc)
            if (c < L'0' || c > L'9')
                return false;
        return true;
    }

    bool parse_link(const std::wstring& uri,
                    std::wstring& ugc,
                    std::wstring& url,
                    std::wstring& reason)
    {
        if (!starts_with_ci(uri, L"sc6replay://play"))
        {
            reason = L"Expected sc6replay://play link.";
            return false;
        }

        const size_t query_pos = uri.find(L'?');
        if (query_pos == std::wstring::npos || query_pos + 1 >= uri.size())
        {
            reason = L"Replay link is missing query parameters.";
            return false;
        }

        size_t pos = query_pos + 1;
        while (pos <= uri.size())
        {
            const size_t amp = uri.find(L'&', pos);
            const size_t end = (amp == std::wstring::npos) ? uri.size() : amp;
            const size_t eq = uri.find(L'=', pos);
            if (eq != std::wstring::npos && eq < end)
            {
                const std::wstring key = uri.substr(pos, eq - pos);
                std::wstring value;
                if (!percent_decode_utf8(uri.substr(eq + 1, end - eq - 1),
                                         value))
                {
                    reason = L"Replay link contains invalid percent encoding.";
                    return false;
                }
                if (key == L"ugc")
                    ugc = value;
                else if (key == L"url")
                    url = value;
            }
            if (amp == std::wstring::npos) break;
            pos = amp + 1;
        }

        if (!valid_ugc(ugc))
        {
            reason = L"Replay link has an invalid UGC id.";
            return false;
        }
        if (!starts_with_ci(url, L"https://"))
        {
            reason = L"Replay link must use an HTTPS download URL.";
            return false;
        }
        return true;
    }

    class WinHttpHandle
    {
    public:
        WinHttpHandle() = default;
        explicit WinHttpHandle(HINTERNET h) : m_h(h) {}
        ~WinHttpHandle()
        {
            if (m_h) WinHttpCloseHandle(m_h);
        }
        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;
        HINTERNET get() const { return m_h; }
        HINTERNET* put()
        {
            if (m_h) WinHttpCloseHandle(m_h);
            m_h = nullptr;
            return &m_h;
        }
    private:
        HINTERNET m_h{nullptr};
    };

    bool download_https(const std::wstring& url,
                        std::vector<unsigned char>& out,
                        std::wstring& reason)
    {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
            parts.nScheme != INTERNET_SCHEME_HTTPS ||
            parts.dwHostNameLength == 0)
        {
            reason = L"Could not parse HTTPS replay URL.";
            return false;
        }

        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath ? parts.lpszUrlPath : L"",
                          parts.dwUrlPathLength);
        if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (path.empty()) path = L"/";

        WinHttpHandle session(WinHttpOpen(
            L"HorseReplayLauncher/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
        if (!session.get())
        {
            reason = L"WinHTTP session creation failed.";
            return false;
        }
        WinHttpSetTimeouts(session.get(), 5000, 5000, 30000, 30000);

        WinHttpHandle connect(WinHttpConnect(
            session.get(), host.c_str(), parts.nPort, 0));
        if (!connect.get())
        {
            reason = L"Could not connect to replay host.";
            return false;
        }

        WinHttpHandle request(WinHttpOpenRequest(
            connect.get(), L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE));
        if (!request.get())
        {
            reason = L"Could not create replay download request.";
            return false;
        }

        if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS,
                                0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.get(), nullptr))
        {
            reason = L"Replay download request failed.";
            return false;
        }

        DWORD status = 0;
        DWORD status_len = sizeof(status);
        if (!WinHttpQueryHeaders(
                request.get(),
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len,
                WINHTTP_NO_HEADER_INDEX) || status != 200)
        {
            wchar_t buf[128]{};
            std::swprintf(buf, 128, L"Replay download returned HTTP %lu.",
                          static_cast<unsigned long>(status));
            reason = buf;
            return false;
        }

        out.clear();
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &available))
            {
                reason = L"Replay download failed while reading.";
                return false;
            }
            if (available == 0) break;
            if (out.size() + available > kMaxReplayBytes)
            {
                reason = L"Replay file is larger than HorseMod's 64 MB cap.";
                return false;
            }
            const size_t old_size = out.size();
            out.resize(old_size + available);
            DWORD read = 0;
            if (!WinHttpReadData(request.get(), out.data() + old_size,
                                 available, &read))
            {
                reason = L"Replay download failed while copying bytes.";
                return false;
            }
            out.resize(old_size + read);
        }

        if (out.size() < 4 || out[0] != 'U' || out[1] != 'L' ||
            out[2] != 'X' || out[3] != '1')
        {
            reason = L"Downloaded replay is not a native ULX1 replay.";
            return false;
        }
        return true;
    }

    bool write_bytes_atomic(const std::wstring& path,
                            const std::vector<unsigned char>& bytes,
                            std::wstring& reason)
    {
        std::wstring tmp = path + L".tmp.";
        tmp += std::to_wstring(GetCurrentProcessId());

        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            reason = L"Could not open replay cache file for writing.";
            return false;
        }
        DWORD written = 0;
        const BOOL ok = WriteFile(h, bytes.data(),
                                  static_cast<DWORD>(bytes.size()),
                                  &written, nullptr);
        FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok || written != bytes.size())
        {
            DeleteFileW(tmp.c_str());
            reason = L"Could not write replay cache file.";
            return false;
        }
        if (!MoveFileExW(tmp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tmp.c_str());
            reason = L"Could not publish replay cache file.";
            return false;
        }
        return true;
    }

    std::string json_escape(const std::string& in)
    {
        std::string out;
        out.reserve(in.size() + 8);
        for (unsigned char c : in)
        {
            switch (c)
            {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
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

    bool write_text_atomic(const std::wstring& path,
                           const std::string& text,
                           std::wstring& reason)
    {
        std::vector<unsigned char> bytes(text.begin(), text.end());
        return write_bytes_atomic(path, bytes, reason);
    }

    bool is_game_running()
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        bool found = false;
        if (Process32FirstW(snap, &pe))
        {
            do
            {
                if (_wcsicmp(pe.szExeFile, L"SoulcaliburVI.exe") == 0)
                {
                    found = true;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return found;
    }

    std::wstring find_game_exe(std::wstring start)
    {
        for (int i = 0; i < 8 && !start.empty(); ++i)
        {
            std::wstring candidate = start;
            if (!candidate.empty() && candidate.back() != L'\\')
                candidate += L'\\';
            candidate += L"SoulcaliburVI.exe";
            const DWORD attrs = GetFileAttributesW(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return candidate;
            start = dirname(start);
        }
        return {};
    }

    bool launch_game(const std::wstring& mod_root)
    {
        const std::wstring exe = find_game_exe(mod_root);
        HINSTANCE result = nullptr;
        if (!exe.empty())
        {
            result = ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                                   dirname(exe).c_str(), SW_SHOWNORMAL);
        }
        else
        {
            result = ShellExecuteW(nullptr, L"open", kSteamRunGameUrl,
                                   nullptr, nullptr, SW_SHOWNORMAL);
        }
        return reinterpret_cast<intptr_t>(result) > 32;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 2)
        fail(L"Missing sc6replay:// link argument.");

    const std::wstring uri = argv[1];
    LocalFree(argv);

    std::wstring ugc;
    std::wstring url;
    std::wstring reason;
    if (!parse_link(uri, ugc, url, reason))
        fail(reason);

    const std::wstring exe_path = module_path();
    std::wstring tools_dir = dirname(exe_path);
    std::wstring mod_root = dirname(tools_dir);
    if (mod_root.empty())
        fail(L"Could not locate HorseMod folder from launcher path.");

    std::wstring saved_dir = mod_root + L"Saved\\";
    std::wstring replay_dir = saved_dir + L"ReplayFiles\\";
    if (!ensure_dir(saved_dir) || !ensure_dir(replay_dir))
        fail(L"Could not create HorseMod Saved\\ReplayFiles folder.");

    std::vector<unsigned char> replay;
    log_line(L"download start ugc=%s url=%s", ugc.c_str(), url.c_str());
    if (!download_https(url, replay, reason))
        fail(reason);

    const std::wstring replay_path = replay_dir + L"REPLAY_" + ugc + L".bin";
    if (!write_bytes_atomic(replay_path, replay, reason))
        fail(reason);

    const std::string replay_path_utf8 = wide_to_utf8(replay_path);
    const std::string run_id = "web-" + wide_to_utf8(ugc);
    std::string json;
    json += "{\n";
    json += "  \"enabled\": true,\n";
    json += "  \"run_id\": \"" + json_escape(run_id) + "\",\n";
    json += "  \"path\": \"" + json_escape(replay_path_utf8) + "\",\n";
    json += "  \"timeout_seconds\": 180,\n";
    json += "  \"timeline_generation_mode\": \"lux-no-render-force\"\n";
    json += "}\n";

    const std::wstring request_path =
        saved_dir + L"replay_file_start_request.json";
    if (!write_text_atomic(request_path, json, reason))
        fail(reason);

    if (!is_game_running() && !launch_game(mod_root))
        fail(L"Replay cached, but SoulcaliburVI could not be launched.");

    log_line(L"queued ugc=%s bytes=%zu replay=%s request=%s",
             ugc.c_str(), replay.size(), replay_path.c_str(),
             request_path.c_str());
    return 0;
}
