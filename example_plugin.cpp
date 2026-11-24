// ============================================================================
//  Example plugin: listen to launcher events, log them / block downloads
//  Build: D:\Dev-Cpp\MinGW64\bin\g++.exe -O2 -std=c++14 -shared -o example_plugin.dll example_plugin.cpp
//  After building, put example_plugin.dll into the launcher plugins\ subdirectory.
// ============================================================================

#include "plugin_api.h"
#include <string.h>

static const PluginHost* g_host = 0;

static int onEvent(const PluginEvent* ev) {
    switch (ev->type) {
    case EVENT_GAME_LAUNCHING:
        g_host->log("INFO", "\u793A\u4F8B\u63D2\u4EF6: \u68C0\u6D4B\u5230\u542F\u52A8\u6E38\u620F, \u7248\u672C = ");
        if (ev->message) g_host->log("INFO", ev->message);
        break;
    case EVENT_DOWNLOAD_START:
        if (ev->message) {
            g_host->log("INFO", "\u793A\u4F8B\u63D2\u4EF6: \u5F00\u59CB\u4E0B\u8F7D ");
            g_host->log("INFO", ev->message);
        }
        break;
    case EVENT_GAME_EXITED:
        g_host->log("INFO", "\u793A\u4F8B\u63D2\u4EF6: \u6E38\u620F\u5DF2\u9000\u51FA");
        break;
    default:
        break;
    }
    return 0; // 0 = do not block
}

PLUGIN_EXPORT int plugin_init(const PluginHost* host) {
    if (!host || host->api_version < MLAUNCHER_PLUGIN_API_VERSION) return -1;
    g_host = host;
    host->log("INFO", "\u793A\u4F8B\u63D2\u4EF6\u52A0\u8F7D\u6210\u529F");
    host->register_hook(EVENT_GAME_LAUNCHING, onEvent);
    host->register_hook(EVENT_DOWNLOAD_START, onEvent);
    host->register_hook(EVENT_GAME_EXITED, onEvent);
    return 0;
}

PLUGIN_EXPORT void plugin_shutdown(void) {
    if (g_host) g_host->log("INFO", "\u793A\u4F8B\u63D2\u4EF6\u5378\u8F7D");
    g_host = 0;
}

PLUGIN_EXPORT const char* plugin_name(void) { return "example_plugin"; }
PLUGIN_EXPORT const char* plugin_version(void) { return "1.0"; }
