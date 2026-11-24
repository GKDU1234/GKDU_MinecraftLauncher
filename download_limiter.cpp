// ============================================================================
//  download_limiter.dll - download stall monitor
//  Tracks in-flight downloads; if no download completes for over 5 seconds
//  while downloads are active, logs a warning.
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstring>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static int g_active = 0;
static DWORD g_lastDone = 0;

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    if (ev->type == EVENT_DOWNLOAD_START) {
        g_active++;
        if (g_lastDone == 0) g_lastDone = GetTickCount();
        if (g_active > 0 && (GetTickCount() - g_lastDone) > 5000) {
            g_host->log("WARN", "\u4E0B\u8F7D\u53EF\u80FD\u5361\u4F4F\uFF0C\u8BF7\u68C0\u67E5\u7F51\u7EDC");
        }
    } else if (ev->type == EVENT_DOWNLOAD_DONE) {
        if (g_active > 0) g_active--;
        g_lastDone = GetTickCount();
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_DOWNLOAD_START, onEvent);
    host->register_hook(EVENT_DOWNLOAD_DONE, onEvent);
    host->log("INFO", "\u4E0B\u8F7D\u9650\u901F\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "download_limiter"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
