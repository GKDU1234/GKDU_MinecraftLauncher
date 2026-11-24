// ============================================================================
//  download_stats.dll - summarize download counts per install
//  Counts download start/success/failure events and reports a summary when a
//  version install finishes.
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstring>
#include "plugin_api.h"

static const PluginHost* g_host = 0;
static int g_started = 0, g_ok = 0, g_fail = 0;

static int onEvent(const PluginEvent* ev) {
    if (!g_host) return 0;
    switch (ev->type) {
    case EVENT_DOWNLOAD_START:
        g_started++;
        break;
    case EVENT_DOWNLOAD_DONE:
        if (ev->result == 0) g_ok++;
        else g_fail++;
        break;
    case EVENT_VERSION_INSTALL_DONE: {
        char msg[160];
        snprintf(msg, sizeof(msg),
            "\u672C\u6B21\u5171\u4E0B\u8F7D %d \u4E2A\u6587\u4EF6\uFF0C\u6210\u529F %d \u4E2A\uFF0C\u5931\u8D25 %d \u4E2A",
            g_started, g_ok, g_fail);
        g_host->log("INFO", msg);
        g_started = g_ok = g_fail = 0;
        break;
    }
    default:
        break;
    }
    return 0;
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->register_hook(EVENT_DOWNLOAD_START, onEvent);
    host->register_hook(EVENT_DOWNLOAD_DONE, onEvent);
    host->register_hook(EVENT_VERSION_INSTALL_DONE, onEvent);
    host->log("INFO", "\u4E0B\u8F7D\u7EDF\u8BA1\u63D2\u4EF6\u5DF2\u52A0\u8F7D");
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) { g_host = 0; }
PLUGIN_EXPORT const char* plugin_name(void) { return "download_stats"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
