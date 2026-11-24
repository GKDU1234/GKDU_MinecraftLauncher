// ============================================================================
//  game_time.dll - track Minecraft play time
//  Listens for game launch/exit and appends a row to playtime.csv
//  (date, version, start-time, seconds) next to this DLL.
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <fstream>
#include <mutex>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;
static time_t g_startTime = 0;
static std::string g_version;
static std::mutex g_mtx;

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

static std::string dllDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_hModule, buf, MAX_PATH);
    if (n == 0) return "";
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return "";
    return wUtf8(p.substr(0, pos));
}

static std::string fmtTime(time_t t, bool dateOnly) {
    struct tm* tmv = localtime(&t);
    if (!tmv) return "";
    char buf[32];
    if (dateOnly) strftime(buf, sizeof(buf), "%Y-%m-%d", tmv);
    else strftime(buf, sizeof(buf), "%H:%M:%S", tmv);
    return std::string(buf);
}

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    if (ev->type == EVENT_GAME_LAUNCHING) {
        g_startTime = time(NULL);
        g_version = ev->message ? ev->message : "";
        return 0;
    }
    if (ev->type == EVENT_GAME_EXITED) {
        if (g_startTime == 0) return 0;
        time_t now = time(NULL);
        long long secs = (long long)(now - g_startTime);
        if (secs < 0) secs = 0;
        g_startTime = 0;

        std::string csv = dllDir() + "\\playtime.csv";
        std::lock_guard<std::mutex> lk(g_mtx);
        std::ofstream f(csv.c_str(), std::ios::app);
        if (f.is_open()) {
            f << fmtTime(now, true) << "," << g_version << ","
              << fmtTime(now - secs, false) << "," << secs << "\n";
            f.close();
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "\u672C\u6B21\u6E38\u73A9 %lld \u5206\u949F", (secs + 59) / 60);
        g_host->log("INFO", msg);
        return 0;
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_GAME_LAUNCHING, onEvent);
    host->register_hook(EVENT_GAME_EXITED, onEvent);
    host->log("INFO", "\u6E38\u620F\u65F6\u957F\u7EDF\u8BA1\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "game_time"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
