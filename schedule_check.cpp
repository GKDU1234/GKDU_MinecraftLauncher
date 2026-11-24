// ============================================================================
//  schedule_check.dll - periodic new-version check
//  On launcher start, spawns a background thread that checks the Mojang
//  version manifest every 6 hours and logs when a new release appears.
// ============================================================================

#include <windows.h>
#include <winhttp.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;

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

static bool httpGetText(const std::wstring& url, std::string& out) {
    std::wstring host = url, path = L"/";
    size_t p = url.find(L"://");
    if (p != std::wstring::npos) host = url.substr(p + 3);
    size_t slash = host.find(L'/');
    if (slash != std::wstring::npos) { path = host.substr(slash); host = host.substr(0, slash); }

    HINTERNET sess = WinHttpOpen(L"GDML-schedule", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!sess) return false;
    HINTERNET conn = WinHttpConnect(sess, host.c_str(), 443, 0);
    if (!conn) { WinHttpCloseHandle(sess); return false; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    bool ok = false;
    if (req) {
        if (WinHttpSendRequest(req, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(req, NULL)) {
            std::vector<unsigned char> buf;
            DWORD avail = 0, read = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                std::vector<unsigned char> chunk(avail);
                if (!WinHttpReadData(req, &chunk[0], avail, &read)) break;
                buf.insert(buf.end(), chunk.begin(), chunk.begin() + read);
                if (read == 0) break;
            }
            out.assign(buf.begin(), buf.end());
            ok = !out.empty();
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);
    return ok;
}

static std::string extractRelease(const std::string& json) {
    const char* key = "\"release\":\"";
    size_t p = json.find(key);
    if (p == std::string::npos) return "";
    p += strlen(key);
    size_t e = json.find('"', p);
    if (e == std::string::npos) return "";
    return json.substr(p, e - p);
}

static void worker() {
    const std::wstring url = L"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
    std::string lastFile = dllDir() + "\\last_version.txt";
    while (g_host) {
        std::string json;
        if (httpGetText(url, json)) {
            std::string rel = extractRelease(json);
            if (!rel.empty()) {
                std::string last;
                std::ifstream in(lastFile.c_str());
                if (in.is_open()) { std::getline(in, last); in.close(); }
                if (last != rel) {
                    std::ofstream out(lastFile.c_str());
                    if (out.is_open()) { out << rel << "\n"; out.close(); }
                    std::string msg = "\u68C0\u6D4B\u5230\u65B0\u7248\u672C: " + rel;
                    g_host->log("INFO", msg.c_str());
                }
            }
        }
        // sleep 6 hours (check g_host each second to allow shutdown detection)
        for (int i = 0; i < 6 * 3600 && g_host; i++) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    if (ev->type == EVENT_LAUNCHER_START) {
        std::thread t(worker);
        t.detach();
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_LAUNCHER_START, onEvent);
    host->log("INFO", "\u5B9A\u65F6\u68C0\u67E5\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "schedule_check"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
