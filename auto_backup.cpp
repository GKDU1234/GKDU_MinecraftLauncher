// ============================================================================
//  auto_backup.dll - back up the saves folder on game exit
//  Recursively copies gameDir\saves into backups\<version>\<timestamp>\
//  next to this DLL and keeps only the newest 5 backups per version.
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;
static std::string g_version;

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

static void copyDirRecursive(const std::wstring& src, const std::wstring& dst) {
    CreateDirectoryW(dst.c_str(), NULL);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((src + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") continue;
        std::wstring sp = src + L"\\" + name;
        std::wstring dp = dst + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            copyDirRecursive(sp, dp);
        } else {
            CopyFileW(sp.c_str(), dp.c_str(), FALSE);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Delete the oldest backups beyond keepCount for a given version dir.
static void pruneBackups(const std::wstring& verDir, int keepCount) {
    std::vector<std::wstring> dirs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((verDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name(fd.cFileName);
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) dirs.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if ((int)dirs.size() <= keepCount) return;
    std::sort(dirs.begin(), dirs.end()); // timestamp names sort chronologically
    int toDelete = (int)dirs.size() - keepCount;
    for (int i = 0; i < toDelete; i++) {
        std::wstring p = verDir + L"\\" + dirs[i];
        // recursively delete: empty files then directory
        WIN32_FIND_DATAW fd2;
        HANDLE h2 = FindFirstFileW((p + L"\\*").c_str(), &fd2);
        if (h2 != INVALID_HANDLE_VALUE) {
            do {
                std::wstring n2(fd2.cFileName);
                if (n2 == L"." || n2 == L"..") continue;
                DeleteFileW((p + L"\\" + n2).c_str());
            } while (FindNextFileW(h2, &fd2));
            FindClose(h2);
        }
        RemoveDirectoryW(p.c_str());
    }
}

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    if (ev->type == EVENT_GAME_LAUNCHING) {
        g_version = ev->message ? ev->message : "";
        return 0;
    }
    if (ev->type == EVENT_GAME_EXITED) {
        const char* gd = g_host->get_setting("gameDir");
        if (!gd || !*gd) return 0;

        std::wstring saves = u8w(std::string(gd)) + L"\\saves";
        DWORD attr = GetFileAttributesW(saves.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return 0;

        std::wstring ver = g_version.empty() ? L"unknown" : u8w(g_version);
        std::wstring verDir = dllDirW() + L"\\backups\\" + ver;
        std::wstring dst = verDir + L"\\" + timestampW();
        copyDirRecursive(saves, dst);
        pruneBackups(verDir, 5);

        std::string log = "\u5907\u4EFD\u5B8C\u6210: " + wUtf8(dst);
        g_host->log("INFO", log.c_str());
        return 0;
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_GAME_LAUNCHING, onEvent);
    host->register_hook(EVENT_GAME_EXITED, onEvent);
    host->log("INFO", "\u5B58\u6863\u81EA\u52A8\u5907\u4EFD\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "auto_backup"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
