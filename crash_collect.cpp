// ============================================================================
//  crash_collect.dll - auto-archive crash reports on game exit
//  Scans gameDir\crash-reports\ for the newest .txt and copies it into
//  crashes\ next to this DLL (with a timestamp prefix) if it was written
//  after the game was launched.
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;
static FILETIME g_launchTime;

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_hModule = h;
    return TRUE;
}

static std::string wUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static std::wstring u8w(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring dllDirW() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hModule, buf, MAX_PATH);
    if (n == 0) return L"";
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return p.substr(0, pos);
}

static std::wstring timestampW() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04d%02d%02d_%02d%02d%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    if (ev->type == EVENT_GAME_LAUNCHING) {
        GetSystemTimeAsFileTime(&g_launchTime);
        return 0;
    }
    if (ev->type == EVENT_GAME_EXITED) {
        const char* gd = g_host->get_setting("gameDir");
        if (!gd || !*gd) return 0;

        std::wstring reportDir = u8w(std::string(gd)) + L"\\crash-reports";
        std::wstring pattern = reportDir + L"\\*.txt";

        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;

        std::wstring newest;
        FILETIME newestTime;
        bool found = false;
        do {
            std::wstring name(fd.cFileName);
            if (name == L"." || name == L"..") continue;
            if (!found || CompareFileTime(&fd.ftLastWriteTime, &newestTime) > 0) {
                newest = name;
                newestTime = fd.ftLastWriteTime;
                found = true;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);

        if (!found) return 0;
        // Only archive if the report was written after the game launched
        if (CompareFileTime(&newestTime, &g_launchTime) < 0) return 0;

        std::wstring src = reportDir + L"\\" + newest;
        std::wstring outDir = dllDirW() + L"\\crashes";
        CreateDirectoryW(outDir.c_str(), NULL);
        std::wstring dst = outDir + L"\\" + timestampW() + L"_" + newest;

        if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
            std::string log = "\u5DF2\u5F52\u6863\u5D29\u6E83\u62A5\u544A: " + wUtf8(dst);
            g_host->log("INFO", log.c_str());
        }
        return 0;
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_GAME_LAUNCHING, onEvent);
    host->register_hook(EVENT_GAME_EXITED, onEvent);
    host->log("INFO", "\u5D29\u6E83\u6536\u96C6\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "crash_collect"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
