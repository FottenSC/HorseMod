#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <Winhttp.h>
#include <TlHelp32.h>
#include <Objbase.h>
#include <Propkey.h>
#include <ShObjIdl.h>
#include <ShlObj.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#include "HorseReplayLauncherResource.h"

#ifndef HORSEMOD_VERSION
#define HORSEMOD_VERSION "dev"
#endif

namespace
{
    constexpr size_t kMaxReplayBytes = 64ull * 1024ull * 1024ull;
    constexpr uint16_t kStatusPort = 54475;
    constexpr char kStatusVersion[] = HORSEMOD_VERSION;
    constexpr wchar_t kSteamRunGameUrl[] = L"steam://rungameid/544750";
    constexpr wchar_t kStatusServerMutex[] =
        L"Local\\HorseModReplayStatusServer";
    constexpr wchar_t kAppUserModelId[] =
        L"no.horseface.HorseMod.ReplayLauncher";

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

    std::wstring roaming_appdata_path()
    {
        wchar_t base[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return {};
        std::wstring path = base;
        if (!path.empty() && path.back() != L'\\') path += L'\\';
        return path;
    }

    bool init_propvariant_from_string(
        const wchar_t* value,
        PROPVARIANT& prop)
    {
        PropVariantInit(&prop);
        prop.vt = VT_LPWSTR;
        const size_t chars = std::wcslen(value) + 1;
        prop.pwszVal = static_cast<wchar_t*>(
            CoTaskMemAlloc(chars * sizeof(wchar_t)));
        if (!prop.pwszVal)
        {
            PropVariantInit(&prop);
            return false;
        }
        std::wmemcpy(prop.pwszVal, value, chars);
        return true;
    }

    void ensure_notification_identity_shortcut()
    {
        const std::wstring appdata = roaming_appdata_path();
        const std::wstring exe_path = module_path();
        if (appdata.empty() || exe_path.empty())
        {
            log_line(L"notification identity shortcut skipped: missing path");
            return;
        }

        std::wstring shortcut_dir =
            appdata +
            L"Microsoft\\Windows\\Start Menu\\Programs\\HorseMod\\";
        if (!ensure_dir(shortcut_dir))
        {
            log_line(L"notification identity shortcut dir failed: %s",
                     shortcut_dir.c_str());
            return;
        }
        const std::wstring shortcut_path =
            shortcut_dir + L"HorseMod Replay Launcher.lnk";

        const HRESULT coinit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool should_uninitialize = SUCCEEDED(coinit);
        if (FAILED(coinit) && coinit != RPC_E_CHANGED_MODE)
        {
            log_line(L"notification identity COM init failed: 0x%08lx",
                     static_cast<unsigned long>(coinit));
            return;
        }

        IShellLinkW* link = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&link));
        if (FAILED(hr) || !link)
        {
            log_line(L"notification identity shell link create failed: 0x%08lx",
                     static_cast<unsigned long>(hr));
            if (should_uninitialize) CoUninitialize();
            return;
        }

        link->SetPath(exe_path.c_str());
        link->SetDescription(L"HorseMod Replay Launcher");
        link->SetIconLocation(exe_path.c_str(), 0);

        IPropertyStore* store = nullptr;
        hr = link->QueryInterface(IID_PPV_ARGS(&store));
        if (SUCCEEDED(hr) && store)
        {
            PROPVARIANT app_id{};
            if (init_propvariant_from_string(kAppUserModelId, app_id))
            {
                hr = store->SetValue(PKEY_AppUserModel_ID, app_id);
                PropVariantClear(&app_id);
                if (SUCCEEDED(hr)) hr = store->Commit();
            }
            else
            {
                hr = E_OUTOFMEMORY;
            }
            if (FAILED(hr))
            {
                log_line(L"notification identity appid set failed: 0x%08lx",
                         static_cast<unsigned long>(hr));
            }
            store->Release();
        }
        else
        {
            log_line(L"notification identity property store failed: 0x%08lx",
                     static_cast<unsigned long>(hr));
        }

        IPersistFile* persist = nullptr;
        hr = link->QueryInterface(IID_PPV_ARGS(&persist));
        if (SUCCEEDED(hr) && persist)
        {
            hr = persist->Save(shortcut_path.c_str(), TRUE);
            if (FAILED(hr))
            {
                log_line(L"notification identity shortcut save failed: 0x%08lx",
                         static_cast<unsigned long>(hr));
            }
            else
            {
                log_line(L"notification identity shortcut ready: %s",
                         shortcut_path.c_str());
                SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr,
                               nullptr);
            }
            persist->Release();
        }
        else
        {
            log_line(L"notification identity persist failed: 0x%08lx",
                     static_cast<unsigned long>(hr));
        }

        link->Release();
        if (should_uninitialize) CoUninitialize();
    }

    void ensure_notification_app_identity()
    {
        static bool attempted = false;
        if (attempted) return;
        attempted = true;

        const HRESULT hr =
            SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
        if (FAILED(hr))
        {
            log_line(L"notification AppUserModelID failed: 0x%08lx",
                     static_cast<unsigned long>(hr));
        }
        ensure_notification_identity_shortcut();
    }

    [[noreturn]] void fail(const std::wstring& message)
    {
        log_line(L"error: %s", message.c_str());
        MessageBoxW(nullptr, message.c_str(), L"HorseMod replay link",
                    MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }

    template <size_t N>
    void copy_notification_text(wchar_t (&dst)[N], const wchar_t* src)
    {
        dst[0] = L'\0';
        if (src && *src)
            (void)wcsncpy_s(dst, N, src, _TRUNCATE);
    }

    HICON load_launcher_icon(int width, int height)
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        HICON icon = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(IDI_HORSE_REPLAY_APP), IMAGE_ICON,
            width, height, LR_DEFAULTCOLOR));
        if (!icon)
        {
            icon = static_cast<HICON>(LoadImageW(
                instance, MAKEINTRESOURCEW(IDI_HORSE_REPLAY_NOTIFICATION),
                IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
        }
        if (!icon)
        {
            icon = static_cast<HICON>(LoadImageW(
                nullptr, MAKEINTRESOURCEW(32516), IMAGE_ICON, width, height,
                LR_SHARED));
        }
        if (!icon)
            icon = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));
        return icon;
    }

    LRESULT CALLBACK notification_window_proc(
        HWND hwnd,
        UINT msg,
        WPARAM wparam,
        LPARAM lparam)
    {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    HWND create_notification_window()
    {
        constexpr wchar_t kClassName[] =
            L"HorseModReplayLauncherNotificationWindow";
        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = notification_window_proc;
        wc.hInstance = instance;
        wc.lpszClassName = kClassName;
        wc.hIcon = load_launcher_icon(GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON));
        wc.hIconSm = load_launcher_icon(GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON));
        if (!RegisterClassExW(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return nullptr;
        }

        return CreateWindowExW(0, kClassName, L"HorseMod", WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, instance,
                               nullptr);
    }

    void pump_notification_messages(DWORD milliseconds)
    {
        const DWORD start = GetTickCount();
        MSG msg{};
        for (;;)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            const DWORD elapsed = GetTickCount() - start;
            if (elapsed >= milliseconds) break;
            const DWORD remaining = milliseconds - elapsed;
            MsgWaitForMultipleObjects(
                0, nullptr, FALSE, std::min<DWORD>(remaining, 50),
                QS_ALLINPUT);
        }
    }

    bool show_starting_replay_notification_with_size(
        HWND hwnd,
        HICON tray_icon,
        HICON balloon_icon,
        DWORD notify_icon_data_size,
        const wchar_t* size_label)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = notify_icon_data_size;
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_APP + 1;
        nid.hIcon = tray_icon;
        copy_notification_text(nid.szTip, L"HorseMod");

        if (!Shell_NotifyIconW(NIM_ADD, &nid))
        {
            log_line(L"notification add failed (%s): %lu", size_label,
                     GetLastError());
            return false;
        }

        NOTIFYICONDATAW version_nid = nid;
        version_nid.uVersion =
#ifdef NOTIFYICON_VERSION_4
            (notify_icon_data_size == sizeof(NOTIFYICONDATAW))
                ? NOTIFYICON_VERSION_4
                :
#endif
                NOTIFYICON_VERSION;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &version_nid))
        {
            log_line(L"notification set-version failed (%s): %lu",
                     size_label, GetLastError());
        }

        NOTIFYICONDATAW info_nid{};
        info_nid.cbSize = notify_icon_data_size;
        info_nid.hWnd = hwnd;
        info_nid.uID = 1;
        info_nid.uFlags = NIF_INFO | NIF_ICON;
        info_nid.hIcon = balloon_icon ? balloon_icon : tray_icon;
        info_nid.uTimeout = 10000;
        info_nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_NOSOUND;
        info_nid.hBalloonIcon = balloon_icon ? balloon_icon : tray_icon;
        copy_notification_text(info_nid.szInfoTitle, L"HorseMod");
        copy_notification_text(info_nid.szInfo,
                               L"Starting replay in Soulcalibur VI.");
        if (!Shell_NotifyIconW(NIM_MODIFY, &info_nid))
        {
            log_line(L"notification modify failed (%s): %lu", size_label,
                     GetLastError());
            (void)Shell_NotifyIconW(NIM_DELETE, &nid);
            return false;
        }

        log_line(L"notification shown (%s)", size_label);
        pump_notification_messages(3500);
        (void)Shell_NotifyIconW(NIM_DELETE, &nid);
        return true;
    }

    void show_starting_replay_notification()
    {
        HWND hwnd = create_notification_window();
        if (!hwnd)
        {
            log_line(L"notification window creation failed: %lu",
                     GetLastError());
            return;
        }

        HICON tray_icon = load_launcher_icon(GetSystemMetrics(SM_CXSMICON),
                                             GetSystemMetrics(SM_CYSMICON));
        HICON balloon_icon = load_launcher_icon(64, 64);

#ifdef NOTIFYICONDATAW_V3_SIZE
        if (!show_starting_replay_notification_with_size(
                hwnd, tray_icon, balloon_icon, NOTIFYICONDATAW_V3_SIZE, L"v3"))
        {
            (void)show_starting_replay_notification_with_size(
                hwnd, tray_icon, balloon_icon, sizeof(NOTIFYICONDATAW),
                L"full");
        }
#else
        (void)show_starting_replay_notification_with_size(
            hwnd, tray_icon, balloon_icon, sizeof(NOTIFYICONDATAW), L"full");
#endif

        DestroyWindow(hwnd);
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

    bool equals_ci(const std::wstring& text, const wchar_t* expected)
    {
        size_t i = 0;
        for (; expected[i]; ++i)
        {
            if (i >= text.size()) return false;
            if (std::towlower(text[i]) != std::towlower(expected[i]))
                return false;
        }
        return i == text.size();
    }

    bool valid_ugc(const std::wstring& ugc)
    {
        if (ugc.empty() || ugc.size() > 20) return false;
        for (wchar_t c : ugc)
            if (c < L'0' || c > L'9')
                return false;
        return true;
    }

    bool validate_replay_download_url(const std::wstring& url,
                                      const std::wstring& ugc,
                                      std::wstring& reason)
    {
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        parts.dwUserNameLength = static_cast<DWORD>(-1);
        parts.dwPasswordLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
            parts.dwHostNameLength == 0)
        {
            reason = L"Could not parse replay download URL.";
            return false;
        }

        if (parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0)
        {
            reason = L"Replay download URL must not include credentials.";
            return false;
        }
        if (parts.dwExtraInfoLength != 0)
        {
            reason = L"Replay download URL must not include query or fragment data.";
            return false;
        }

        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath ? parts.lpszUrlPath : L"",
                          parts.dwUrlPathLength);
        if (path.empty()) path = L"/";
        const std::wstring expected_path =
            L"/api/replays/" + ugc + L"/file";
        if (path != expected_path)
        {
            reason = L"Replay download URL path does not match the UGC id.";
            return false;
        }

        if (parts.nScheme == INTERNET_SCHEME_HTTPS &&
            equals_ci(host, L"api-replay.horseface.no"))
        {
            if (parts.nPort != INTERNET_DEFAULT_HTTPS_PORT)
            {
                reason = L"Production replay download URL must use HTTPS port 443.";
                return false;
            }
            return true;
        }

        const bool local_host =
            equals_ci(host, L"localhost") || host == L"127.0.0.1";
        if (parts.nScheme == INTERNET_SCHEME_HTTP && local_host)
            return true;

        reason = L"Replay download URL is not from an allowed archive host.";
        return false;
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
        return validate_replay_download_url(url, ugc, reason);
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

    bool download_replay_url(const std::wstring& url,
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
            (parts.nScheme != INTERNET_SCHEME_HTTPS &&
             parts.nScheme != INTERNET_SCHEME_HTTP) ||
            parts.dwHostNameLength == 0)
        {
            reason = L"Could not parse replay download URL.";
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
            parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
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

    std::wstring lowercase_slash_normalized(std::wstring path)
    {
        for (wchar_t& c : path)
        {
            if (c == L'/') c = L'\\';
            else c = static_cast<wchar_t>(std::towlower(c));
        }
        return path;
    }

    std::wstring launcher_mod_root()
    {
        const std::wstring exe_path = module_path();
        const std::wstring tools_dir = dirname(exe_path);
        return dirname(tools_dir);
    }

    bool is_shimloader_profile_install(const std::wstring& mod_root)
    {
        const std::wstring path = lowercase_slash_normalized(mod_root);
        return path.find(L"\\shimloader\\mod\\") != std::wstring::npos;
    }

    bool file_exists(const std::wstring& path)
    {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES &&
               (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool dir_exists(const std::wstring& path)
    {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES &&
               (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    std::wstring append_path(std::wstring base, const wchar_t* leaf)
    {
        if (!base.empty() && base.back() != L'\\' && base.back() != L'/')
            base += L'\\';
        base += leaf;
        return base;
    }

    std::wstring normalize_backslashes(std::wstring path)
    {
        for (wchar_t& c : path)
        {
            if (c == L'/') c = L'\\';
        }
        return path;
    }

    std::wstring normalize_forward_slashes(std::wstring path)
    {
        for (wchar_t& c : path)
        {
            if (c == L'\\') c = L'/';
        }
        return path;
    }

    std::wstring quote_arg(const std::wstring& arg)
    {
        std::wstring out = L"\"";
        size_t slash_count = 0;
        for (wchar_t c : arg)
        {
            if (c == L'\\')
            {
                ++slash_count;
                continue;
            }
            if (c == L'"')
            {
                out.append(slash_count * 2 + 1, L'\\');
                out.push_back(c);
            }
            else
            {
                out.append(slash_count, L'\\');
                out.push_back(c);
            }
            slash_count = 0;
        }
        out.append(slash_count * 2, L'\\');
        out.push_back(L'"');
        return out;
    }

    bool read_reg_string(HKEY root,
                         const wchar_t* subkey,
                         const wchar_t* value,
                         std::wstring& out)
    {
        DWORD type = 0;
        DWORD bytes = 0;
        LSTATUS status = RegGetValueW(
            root, subkey, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
            &type, nullptr, &bytes);
        if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t))
            return false;

        std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
        status = RegGetValueW(
            root, subkey, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
            &type, &buffer[0], &bytes);
        if (status != ERROR_SUCCESS)
            return false;

        while (!buffer.empty() && buffer.back() == L'\0')
            buffer.pop_back();
        if (buffer.empty())
            return false;
        out = buffer;
        return true;
    }

    std::wstring env_path(const wchar_t* name, const wchar_t* suffix)
    {
        wchar_t base[MAX_PATH]{};
        const DWORD n = GetEnvironmentVariableW(name, base, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
            return {};
        return append_path(base, suffix);
    }

    std::wstring find_steam_exe()
    {
        std::vector<std::wstring> candidates;

        std::wstring value;
        if (read_reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam",
                            L"SteamExe", value))
            candidates.push_back(normalize_backslashes(value));
        if (read_reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam",
                            L"InstallPath", value))
            candidates.push_back(append_path(normalize_backslashes(value),
                                             L"steam.exe"));
        if (read_reg_string(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                            L"InstallPath", value))
            candidates.push_back(append_path(normalize_backslashes(value),
                                             L"steam.exe"));
        if (read_reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam",
                            L"InstallPath", value))
            candidates.push_back(append_path(normalize_backslashes(value),
                                             L"steam.exe"));

        const std::wstring program_files_x86 =
            env_path(L"ProgramFiles(x86)", L"Steam\\steam.exe");
        if (!program_files_x86.empty())
            candidates.push_back(program_files_x86);
        const std::wstring program_files =
            env_path(L"ProgramFiles", L"Steam\\steam.exe");
        if (!program_files.empty())
            candidates.push_back(program_files);

        for (const std::wstring& candidate : candidates)
        {
            if (file_exists(candidate))
                return candidate;
        }
        return {};
    }

    std::wstring shimloader_dir_from_mod_root(const std::wstring& mod_root)
    {
        const std::wstring path = lowercase_slash_normalized(mod_root);
        constexpr wchar_t marker[] = L"\\shimloader\\mod\\";
        const size_t pos = path.find(marker);
        if (pos == std::wstring::npos)
            return {};

        std::wstring profile_root = mod_root.substr(0, pos + 1);
        return append_path(profile_root, L"shimloader");
    }

    void append_profile_arg(std::wstring& args,
                            const wchar_t* name,
                            const std::wstring& path)
    {
        args += L" ";
        args += quote_arg(name);
        args += L" ";
        args += quote_arg(normalize_forward_slashes(path));
    }

    const char* launch_mode_name(const std::wstring& mod_root)
    {
        return is_shimloader_profile_install(mod_root)
                   ? "mod-manager-profile"
                   : "direct";
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

    bool launch_profile_game(const std::wstring& mod_root,
                             const std::wstring& replay_path,
                             std::wstring& reason)
    {
        const std::wstring shimloader_dir =
            shimloader_dir_from_mod_root(mod_root);
        if (shimloader_dir.empty())
        {
            reason = L"Could not derive Thunderstore shimloader profile "
                     L"folders from the HorseMod install path.";
            return false;
        }

        const std::wstring mod_dir = append_path(shimloader_dir, L"mod");
        if (!dir_exists(mod_dir))
        {
            reason = L"Could not find the Thunderstore shimloader mod folder.";
            return false;
        }

        const std::wstring steam_exe = find_steam_exe();
        if (steam_exe.empty())
        {
            reason = L"Could not find steam.exe to launch Soulcalibur VI "
                     L"with this Thunderstore profile.";
            return false;
        }

        std::wstring args = L"-applaunch 544750";
        append_profile_arg(args, L"--mod-dir", mod_dir);
        append_profile_arg(args, L"--pak-dir",
                           append_path(shimloader_dir, L"pak"));
        append_profile_arg(args, L"--cfg-dir",
                           append_path(shimloader_dir, L"cfg"));
        append_profile_arg(args, L"--overlay-dir",
                           append_path(shimloader_dir, L"overlay"));
        append_profile_arg(args, L"--horsemod-replay-file", replay_path);
        append_profile_arg(args, L"--horsemod-replay-generate-mode",
                           L"lux-no-render-force");

        log_line(L"launching Steam profile game steam=%s args=%s",
                 steam_exe.c_str(), args.c_str());
        const HINSTANCE result =
            ShellExecuteW(nullptr, L"open", steam_exe.c_str(), args.c_str(),
                          dirname(steam_exe).c_str(), SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(result) <= 32)
        {
            reason = L"Steam did not accept the Thunderstore profile launch "
                     L"command.";
            return false;
        }
        return true;
    }

    class WsaSession
    {
    public:
        WsaSession()
        {
            WSADATA data{};
            m_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }
        ~WsaSession()
        {
            if (m_ok) WSACleanup();
        }
        bool ok() const { return m_ok; }

    private:
        bool m_ok{false};
    };

    bool send_all(SOCKET s, const char* data, size_t len)
    {
        size_t sent_total = 0;
        while (sent_total < len)
        {
            const int chunk = static_cast<int>(
                std::min<size_t>(len - sent_total, 32 * 1024));
            const int sent = send(s, data + sent_total, chunk, 0);
            if (sent <= 0) return false;
            sent_total += static_cast<size_t>(sent);
        }
        return true;
    }

    bool ascii_iequals(std::string a, std::string b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            unsigned char ca = static_cast<unsigned char>(a[i]);
            unsigned char cb = static_cast<unsigned char>(b[i]);
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
            if (ca != cb) return false;
        }
        return true;
    }

    std::string trim_ascii(std::string value)
    {
        while (!value.empty() &&
               (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());
        while (!value.empty() &&
               (value.back() == ' ' || value.back() == '\t' ||
                value.back() == '\r' || value.back() == '\n'))
            value.pop_back();
        return value;
    }

    std::string header_value(const std::string& request,
                             const char* wanted_name)
    {
        size_t pos = 0;
        for (;;)
        {
            const size_t line_end = request.find("\r\n", pos);
            if (line_end == std::string::npos) break;
            if (line_end == pos) break;
            const std::string line = request.substr(pos, line_end - pos);
            pos = line_end + 2;

            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            if (ascii_iequals(line.substr(0, colon), wanted_name))
                return trim_ascii(line.substr(colon + 1));
        }
        return {};
    }

    bool safe_origin_value(const std::string& origin)
    {
        if (origin.empty() || origin.size() > 512) return false;
        for (char c : origin)
        {
            if (c == '\r' || c == '\n' || c == '\0') return false;
        }
        return true;
    }

    bool allowed_status_origin(const std::string& origin)
    {
        if (!safe_origin_value(origin)) return false;
        return ascii_iequals(origin, "https://replay.horseface.no") ||
               ascii_iequals(origin, "http://localhost:3004") ||
               ascii_iequals(origin, "http://127.0.0.1:3004") ||
               ascii_iequals(origin, "http://localhost:3005") ||
               ascii_iequals(origin, "http://127.0.0.1:3005");
    }

    void send_status_response(SOCKET client,
                              int status,
                              const char* reason,
                              const std::string& body,
                              const std::string& origin)
    {
        const bool add_cors =
            !origin.empty() && allowed_status_origin(origin);
        std::string headers;
        headers += "HTTP/1.1 ";
        headers += std::to_string(status);
        headers += " ";
        headers += reason ? reason : "OK";
        headers += "\r\n";
        headers += "Content-Type: application/json; charset=utf-8\r\n";
        headers += "Cache-Control: no-store\r\n";
        if (add_cors)
        {
            headers += "Access-Control-Allow-Origin: ";
            headers += origin;
            headers += "\r\n";
            headers += "Vary: Origin\r\n";
            headers += "Access-Control-Allow-Methods: GET, OPTIONS\r\n";
            headers +=
                "Access-Control-Allow-Headers: Accept, Content-Type\r\n";
            headers += "Access-Control-Allow-Private-Network: true\r\n";
        }
        headers += "Connection: close\r\n";
        headers += "Content-Length: ";
        headers += std::to_string(body.size());
        headers += "\r\n\r\n";
        (void)send_all(client, headers.data(), headers.size());
        if (!body.empty())
            (void)send_all(client, body.data(), body.size());
    }

    bool parse_http_request_line(const std::string& request,
                                 std::string& method,
                                 std::string& target)
    {
        const size_t line_end = request.find("\r\n");
        const std::string line =
            request.substr(0, line_end == std::string::npos
                                  ? request.size()
                                  : line_end);
        const size_t first_space = line.find(' ');
        if (first_space == std::string::npos) return false;
        const size_t second_space = line.find(' ', first_space + 1);
        if (second_space == std::string::npos) return false;
        method = line.substr(0, first_space);
        target = line.substr(first_space + 1,
                             second_space - first_space - 1);
        return true;
    }

    bool status_target_matches(const std::string& target)
    {
        if (target == "/status") return true;
        return target.rfind("/status?", 0) == 0;
    }

    void handle_status_client(SOCKET client)
    {
        DWORD timeout_ms = 2000;
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                         reinterpret_cast<const char*>(&timeout_ms),
                         sizeof(timeout_ms));
        (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                         reinterpret_cast<const char*>(&timeout_ms),
                         sizeof(timeout_ms));

        char buffer[4096]{};
        const int got = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (got <= 0) return;

        const std::string request(buffer, got);
        const std::string origin = header_value(request, "Origin");
        std::string method;
        std::string target;
        if (!parse_http_request_line(request, method, target))
        {
            send_status_response(
                client, 400, "Bad Request",
                "{\"error\":\"bad_request\"}\n", origin);
            return;
        }

        const bool status_target = status_target_matches(target);
        if (status_target && !origin.empty() &&
            !allowed_status_origin(origin))
        {
            send_status_response(
                client, 403, "Forbidden",
                "{\"error\":\"origin_not_allowed\"}\n", "");
            return;
        }

        if (method == "OPTIONS" && status_target)
        {
            send_status_response(client, 204, "No Content", "", origin);
            return;
        }

        if (method != "GET" || !status_target)
        {
            send_status_response(
                client, 404, "Not Found",
                "{\"error\":\"not_found\"}\n", origin);
            return;
        }

        std::string body;
        body += "{";
        body += "\"installed\":true,";
        body += "\"version\":\"";
        body += kStatusVersion;
        body += "\",";
        body += "\"protocol\":\"sc6replay\",";
        const std::wstring mod_root = launcher_mod_root();
        const bool game_running = is_game_running();
        const bool profile_install = is_shimloader_profile_install(mod_root);
        body += "\"gameRunning\":";
        body += game_running ? "true" : "false";
        body += ",";
        body += "\"launchMode\":\"";
        body += launch_mode_name(mod_root);
        body += "\",";
        body += "\"requiresProfileLaunch\":";
        body += (!game_running && profile_install) ? "true" : "false";
        body += "}\n";
        send_status_response(client, 200, "OK", body, origin);
    }

    DWORD parse_owner_pid_arg(LPWSTR* argv, int argc)
    {
        auto parse_pid = [](const wchar_t* text) -> DWORD {
            if (!text || !*text) return 0;
            wchar_t* end = nullptr;
            const unsigned long value = std::wcstoul(text, &end, 10);
            if (end == text || (end && *end) || value == 0)
                return 0;
            return static_cast<DWORD>(value);
        };

        constexpr wchar_t prefix[] = L"--owner-pid=";
        constexpr size_t prefix_len = (sizeof(prefix) / sizeof(prefix[0])) - 1;
        for (int i = 2; i < argc; ++i)
        {
            const wchar_t* arg = argv[i] ? argv[i] : L"";
            if (std::wcscmp(arg, L"--owner-pid") == 0)
            {
                if (i + 1 < argc)
                    return parse_pid(argv[i + 1]);
                return 0;
            }
            if (std::wcsncmp(arg, prefix, prefix_len) == 0)
                return parse_pid(arg + prefix_len);
        }
        return 0;
    }

    bool status_owner_exited(HANDLE owner_process)
    {
        return owner_process &&
               WaitForSingleObject(owner_process, 0) == WAIT_OBJECT_0;
    }

    int run_status_server(DWORD owner_pid)
    {
        HANDLE mutex = CreateMutexW(nullptr, TRUE, kStatusServerMutex);
        if (!mutex)
        {
            log_line(L"status server mutex creation failed: %lu",
                     GetLastError());
            return 0;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            log_line(L"status server already running");
            CloseHandle(mutex);
            return 0;
        }

        WsaSession wsa;
        if (!wsa.ok())
        {
            log_line(L"status server WSAStartup failed: %d",
                     WSAGetLastError());
            CloseHandle(mutex);
            return 0;
        }

        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            log_line(L"status server socket failed: %d", WSAGetLastError());
            CloseHandle(mutex);
            return 0;
        }

        BOOL exclusive = TRUE;
        (void)setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                         reinterpret_cast<const char*>(&exclusive),
                         sizeof(exclusive));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kStatusPort);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr)) == SOCKET_ERROR)
        {
            log_line(L"status server bind 127.0.0.1:%u failed: %d",
                     static_cast<unsigned>(kStatusPort), WSAGetLastError());
            closesocket(listener);
            CloseHandle(mutex);
            return 0;
        }

        if (listen(listener, SOMAXCONN) == SOCKET_ERROR)
        {
            log_line(L"status server listen failed: %d", WSAGetLastError());
            closesocket(listener);
            CloseHandle(mutex);
            return 0;
        }

        log_line(L"status server listening on 127.0.0.1:%u version=%S",
                 static_cast<unsigned>(kStatusPort), kStatusVersion);

        HANDLE owner_process = nullptr;
        if (owner_pid != 0)
        {
            owner_process = OpenProcess(SYNCHRONIZE, FALSE, owner_pid);
            if (owner_process)
            {
                log_line(L"status server owner pid=%lu", owner_pid);
            }
            else
            {
                log_line(L"status server could not open owner pid=%lu "
                         L"error=%lu",
                         owner_pid, GetLastError());
            }
        }

        for (;;)
        {
            if (status_owner_exited(owner_process))
            {
                log_line(L"status server owner exited; stopping");
                break;
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(listener, &readfds);
            timeval timeout{};
            timeout.tv_sec = 1;

            const int ready =
                select(0, &readfds, nullptr, nullptr, &timeout);
            if (ready == 0)
                continue;
            if (ready == SOCKET_ERROR)
            {
                const int err = WSAGetLastError();
                log_line(L"status server select failed: %d", err);
                Sleep(250);
                continue;
            }

            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                const int err = WSAGetLastError();
                log_line(L"status server accept failed: %d", err);
                Sleep(250);
                continue;
            }
            handle_status_client(client);
            shutdown(client, SD_BOTH);
            closesocket(client);
        }

        if (owner_process)
            CloseHandle(owner_process);
        closesocket(listener);
        CloseHandle(mutex);
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    ensure_notification_app_identity();
    if (argv && argc >= 2 && std::wcscmp(argv[1], L"--test-notification") == 0)
    {
        LocalFree(argv);
        show_starting_replay_notification();
        return 0;
    }
    if (argv && argc >= 2 && std::wcscmp(argv[1], L"--status-server") == 0)
    {
        const DWORD owner_pid = parse_owner_pid_arg(argv, argc);
        LocalFree(argv);
        return run_status_server(owner_pid);
    }
    if (!argv || argc < 2)
        fail(L"Missing sc6replay:// link argument.");

    const std::wstring uri = argv[1];
    LocalFree(argv);

    std::wstring ugc;
    std::wstring url;
    std::wstring reason;
    if (!parse_link(uri, ugc, url, reason))
        fail(reason);

    const std::wstring mod_root = launcher_mod_root();
    if (mod_root.empty())
        fail(L"Could not locate HorseMod folder from launcher path.");

    std::wstring saved_dir = mod_root + L"Saved\\";
    std::wstring replay_dir = saved_dir + L"ReplayFiles\\";
    if (!ensure_dir(saved_dir) || !ensure_dir(replay_dir))
        fail(L"Could not create HorseMod Saved\\ReplayFiles folder.");

    std::vector<unsigned char> replay;
    log_line(L"download start ugc=%s url=%s", ugc.c_str(), url.c_str());
    if (!download_replay_url(url, replay, reason))
        fail(reason);

    const std::wstring replay_path = replay_dir + L"REPLAY_" + ugc + L".bin";
    if (!write_bytes_atomic(replay_path, replay, reason))
        fail(reason);

    const bool game_running = is_game_running();
    if (!game_running && is_shimloader_profile_install(mod_root))
    {
        if (!launch_profile_game(mod_root, replay_path, reason))
        {
            std::wstring message =
                L"Replay downloaded, but Soulcalibur VI could not be "
                L"launched with this Thunderstore profile.\n\n";
            message += reason;
            fail(message);
        }
        log_line(L"launched profile replay ugc=%s bytes=%zu replay=%s",
                 ugc.c_str(), replay.size(), replay_path.c_str());
        show_starting_replay_notification();
        return 0;
    }

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

    log_line(L"queued ugc=%s bytes=%zu replay=%s request=%s",
             ugc.c_str(), replay.size(), replay_path.c_str(),
             request_path.c_str());

    if (!game_running)
    {
        if (!launch_game(mod_root))
            fail(L"Replay cached, but SoulcaliburVI could not be launched.");
    }

    show_starting_replay_notification();
    return 0;
}
