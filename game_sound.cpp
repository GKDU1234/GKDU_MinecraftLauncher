// ============================================================================
//  game_sound.dll - play event sounds
//  Plays launch.wav / exit.wav / done.wav (next to this DLL) on the matching
//  events. Missing files are silently ignored.
// ============================================================================

#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static HMODULE g_hModule = 0;

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_hModule = h;
    return TRUE;
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

static void playIfExists(const wchar_t* fname) {
    std::wstring p = dllDirW() + L"\\" + fname;
    if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
        PlaySoundW(p.c_str(), NULL, SND_FILENAME | SND_ASYNC);
    }
}

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    switch (ev->type) {
    case EVENT_GAME_LAUNCHING: playIfExists(L"launch.wav"); break;
    case EVENT_GAME_EXITED: playIfExists(L"exit.wav"); break;
    case EVENT_VERSION_INSTALL_DONE: playIfExists(L"done.wav"); break;
    default: break;
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_GAME_LAUNCHING, onEvent);
    host->register_hook(EVENT_GAME_EXITED, onEvent);
    host->register_hook(EVENT_VERSION_INSTALL_DONE, onEvent);
    host->log("INFO", "\u97F3\u6548\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "game_sound"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
